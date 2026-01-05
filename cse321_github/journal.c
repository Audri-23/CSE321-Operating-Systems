#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>


#define FS_MAGIC 0x56534653U
#define JOURNAL_MAGIC 0x4A524E4CU
#define BLOCK_SIZE        4096U
#define INODE_SIZE         128U
#define JOURNAL_BLOCK_IDX    1U
#define JOURNAL_BLOCKS      16U
#define INODE_BLOCKS         2U
#define DATA_BLOCKS         64U
#define INODE_BMAP_IDX     (JOURNAL_BLOCK_IDX + JOURNAL_BLOCKS)
#define DATA_BMAP_IDX      (INODE_BMAP_IDX + 1U)
#define INODE_START_IDX    (DATA_BMAP_IDX + 1U)
#define DATA_START_IDX     (INODE_START_IDX + INODE_BLOCKS)
#define TOTAL_BLOCKS       (DATA_START_IDX + DATA_BLOCKS)
#define DEFAULT_IMAGE "vsfs.img"
#define NAME_LEN 28
#define REC_DATA 1
#define REC_COMMIT 2


struct superblock {
    uint32_t magic;
    uint32_t block_size;
    uint32_t total_blocks;
    uint32_t inode_count;
    uint32_t journal_block;
    uint32_t inode_bitmap;
    uint32_t data_bitmap;
    uint32_t inode_start;
    uint32_t data_start;
    uint8_t  _pad[128 - 9 * 4];
};

struct inode {
    uint16_t type;
    uint16_t links;
    uint32_t size;
    uint32_t direct[8];
    uint32_t ctime;
    uint32_t mtime;
    uint8_t _pad[128 - (2 + 2 + 4 + 8 * 4 + 4 + 4)];
};

struct dirent {
    uint32_t inode;
    char name[NAME_LEN];
};

struct journal_header {
    uint32_t magic;
    uint32_t nbytes_used;
};

struct rec_header {
    uint16_t type;
    uint16_t size;
};

struct data_record {
    struct rec_header hdr;
    uint32_t block_no;
    uint8_t data[BLOCK_SIZE];
};

struct commit_record {
    struct rec_header hdr;
};

_Static_assert(sizeof(struct superblock) == 128, "superblock must be 128 bytes");
_Static_assert(sizeof(struct inode) == 128, "inode must be 128 bytes");
_Static_assert(sizeof(struct dirent) == 32, "dirent must be 32 bytes");

static void die(const char *msg) {
    perror(msg);
    exit(EXIT_FAILURE);
}

static void pread_block(int fd, uint32_t block_index, void *buf) {
    off_t offset = (off_t)block_index * BLOCK_SIZE;
    ssize_t n = pread(fd, buf, BLOCK_SIZE, offset);
    if (n != (ssize_t)BLOCK_SIZE) {
        die("pread");
    }
}

static void pwrite_block(int fd, uint32_t block_index, const void *buf) {
    off_t offset = (off_t)block_index * BLOCK_SIZE;
    ssize_t n = pwrite(fd, buf, BLOCK_SIZE, offset);
    if (n != (ssize_t)BLOCK_SIZE) {
        die("pwrite");
    }
}

static void set_bitmap(uint8_t *bitmap, uint32_t index) {
    bitmap[index / 8] |= (uint8_t)(1U << (index % 8));
}

static int test_bitmap(const uint8_t *bitmap, uint32_t index) {
    return (bitmap[index / 8] >> (index % 8)) & 0x1;
}

static uint32_t find_free_inode(const uint8_t *bitmap, uint32_t count) {
    for (uint32_t i = 0; i < count; i++) {
        if (!test_bitmap(bitmap, i)) {
            return i;
        }
    }
    return (uint32_t)-1;
}

static int find_free_dirent_slot(const struct dirent *dirents, uint32_t max_entries) {
    for (uint32_t i = 0; i < max_entries; i++) {
        if (dirents[i].inode == 0 && dirents[i].name[0] == '\0') {
            return i;
        }
    }
    return -1;
}

static void read_journal_area(int fd, uint8_t *journal_area) {
    for (uint32_t i = 0; i < JOURNAL_BLOCKS; i++) {
        pread_block(fd, JOURNAL_BLOCK_IDX + i, journal_area + (i * BLOCK_SIZE));
    }
}

static void w_journal_a(int fd, const uint8_t *journal_area) {
    for (uint32_t i = 0; i < JOURNAL_BLOCKS; i++) {
        pwrite_block(fd, JOURNAL_BLOCK_IDX + i, journal_area + (i * BLOCK_SIZE));
    }
}

static void app_j_to_m(const uint8_t *journal_area, uint8_t *inode_bitmap, uint8_t *inode_table, uint8_t *root_dir, uint32_t root_data_block) {
    struct journal_header *jhdr = (struct journal_header *)journal_area;
    
    if (jhdr->magic != JOURNAL_MAGIC || jhdr->nbytes_used == sizeof(struct journal_header)) {
        return; 
    }

    uint32_t offset = sizeof(struct journal_header);

    while (offset < jhdr->nbytes_used) {
        struct rec_header *rhdr = (struct rec_header *)(journal_area + offset);

        if (rhdr->type == REC_DATA) {
            struct data_record *drec = (struct data_record *)(journal_area + offset);

            if (drec->block_no == INODE_BMAP_IDX) {
                memcpy(inode_bitmap, drec->data, BLOCK_SIZE);
            } else if (drec->block_no == INODE_START_IDX) {
                memcpy(inode_table, drec->data, BLOCK_SIZE);
            } else if (drec->block_no == root_data_block) {
                memcpy(root_dir, drec->data, BLOCK_SIZE);
            }
            
            offset += drec->hdr.size;

        } else if (rhdr->type == REC_COMMIT) {
            offset += rhdr->size;
        } else {
            break;
        }
    }
}

static void create_c(int fd, const char *filename) {
    size_t filename_len = strlen(filename);
    if (filename_len == 0) {
        fprintf(stderr, "Filename cannot be empty\n");
        exit(EXIT_FAILURE);
    }
    if (filename_len >= NAME_LEN) {
        fprintf(stderr, "Filename too long!\n");
        exit(EXIT_FAILURE);
    }

    struct superblock sb;
    pread_block(fd, 0, &sb);

    uint8_t journal_area[JOURNAL_BLOCKS * BLOCK_SIZE];
    read_journal_area(fd, journal_area);

    struct journal_header *jhdr = (struct journal_header *)journal_area;

    if (jhdr->magic != JOURNAL_MAGIC) {
        jhdr->magic = JOURNAL_MAGIC;
        jhdr->nbytes_used = sizeof(struct journal_header);
    }

    uint8_t inode_bitmap[BLOCK_SIZE];
    uint8_t root_dir_block[BLOCK_SIZE];
    uint8_t inode_table_block[BLOCK_SIZE];

    pread_block(fd, INODE_BMAP_IDX, inode_bitmap);
    pread_block(fd, INODE_START_IDX, inode_table_block);

    struct inode *inodes = (struct inode *)inode_table_block;
    struct inode *root_inode = &inodes[0];
    uint32_t root_data_block = root_inode->direct[0];
    pread_block(fd, root_data_block, root_dir_block);


    app_j_to_m(journal_area, inode_bitmap, inode_table_block, root_dir_block, root_data_block);

    struct dirent *dirents = (struct dirent *)root_dir_block;
    inodes = (struct inode *)inode_table_block;

    uint32_t max_entries = BLOCK_SIZE / sizeof(struct dirent);
    for (uint32_t i = 0; i < max_entries; i++) {
        if (dirents[i].inode != 0 || dirents[i].name[0] != '\0') {
            if (strcmp(dirents[i].name, filename) == 0) {
                fprintf(stderr, "File '%s' already exists\n", filename);
                exit(EXIT_FAILURE);
            }
        }
    }

    uint32_t free_inum = find_free_inode(inode_bitmap, sb.inode_count);
    if (free_inum == (uint32_t)-1) {
        fprintf(stderr, "No free inodes available\n");
        exit(EXIT_FAILURE);
    }

    int slot = find_free_dirent_slot(dirents, max_entries);
    if (slot < 0) {
        fprintf(stderr, "No free directory entry slots\n");
        exit(EXIT_FAILURE);
    }

    uint8_t new_inode_bitmap[BLOCK_SIZE];
    uint8_t new_inode_table[BLOCK_SIZE];
    uint8_t new_root_dir[BLOCK_SIZE];

    memcpy(new_inode_bitmap, inode_bitmap, BLOCK_SIZE);
    memcpy(new_inode_table, inode_table_block, BLOCK_SIZE);
    memcpy(new_root_dir, root_dir_block, BLOCK_SIZE);

    set_bitmap(new_inode_bitmap, free_inum);

    struct inode *new_inodes = (struct inode *)new_inode_table;
    struct inode *new_inode = &new_inodes[free_inum];
    memset(new_inode, 0, sizeof(struct inode));
    new_inode->type = 1; 
    new_inode->links = 1;
    new_inode->size = 0;
    time_t now = time(NULL);
    new_inode->ctime = (uint32_t)now;
    new_inode->mtime = (uint32_t)now;

    struct dirent *new_dirents = (struct dirent *)new_root_dir;
    new_dirents[slot].inode = free_inum;
    strncpy(new_dirents[slot].name, filename, NAME_LEN - 1);
    new_dirents[slot].name[NAME_LEN - 1] = '\0';

    new_inodes[0].size = new_inodes[0].size + sizeof(struct dirent);
    new_inodes[0].mtime = (uint32_t)now;

    uint32_t data_rec_size = sizeof(struct rec_header) + sizeof(uint32_t) + BLOCK_SIZE;
    uint32_t commit_rec_size = sizeof(struct rec_header);
    uint32_t total_needed = 3 * data_rec_size + commit_rec_size;

    if (jhdr->nbytes_used + total_needed > JOURNAL_BLOCKS * BLOCK_SIZE) {
        fprintf(stderr, "Insufficient journal space. Please run 'install' first.\n");
        exit(EXIT_FAILURE);
    }

    uint32_t offset = jhdr->nbytes_used;

    struct data_record *drec = (struct data_record *)(journal_area + offset);
    drec->hdr.type = REC_DATA;
    drec->hdr.size = data_rec_size;
    drec->block_no = INODE_BMAP_IDX;
    memcpy(drec->data, new_inode_bitmap, BLOCK_SIZE);
    offset += data_rec_size;

    drec = (struct data_record *)(journal_area + offset);
    drec->hdr.type = REC_DATA;
    drec->hdr.size = data_rec_size;
    drec->block_no = INODE_START_IDX;
    memcpy(drec->data, new_inode_table, BLOCK_SIZE);
    offset += data_rec_size;

    drec = (struct data_record *)(journal_area + offset);
    drec->hdr.type = REC_DATA;
    drec->hdr.size = data_rec_size;
    drec->block_no = root_data_block;
    memcpy(drec->data, new_root_dir, BLOCK_SIZE);
    offset += data_rec_size;

    struct commit_record *crec = (struct commit_record *)(journal_area + offset);
    crec->hdr.type = REC_COMMIT;
    crec->hdr.size = commit_rec_size;
    offset += commit_rec_size;

    jhdr->nbytes_used = offset;
    w_journal_a(fd, journal_area);
    printf("Logged creation of '%s' to journal.\n", filename);
}

static void install_c(int fd) {
    uint8_t journal_area[JOURNAL_BLOCKS * BLOCK_SIZE];
    read_journal_area(fd, journal_area);

    struct journal_header *jhdr = (struct journal_header *)journal_area;

    if (jhdr->magic != JOURNAL_MAGIC) {
        fprintf(stderr, "Journal does not exist\n");
        exit(EXIT_FAILURE);
    }

    if (jhdr->nbytes_used == sizeof(struct journal_header)) {
        printf("Journal is empty. Nothing to install.\n");
        return;
    }

    uint32_t offset = sizeof(struct journal_header);
    int transaction_count = 0;

    while (offset < jhdr->nbytes_used) {
        struct rec_header *rhdr = (struct rec_header *)(journal_area + offset);

        if (rhdr->type == REC_DATA) {
            struct data_record *drec = (struct data_record *)(journal_area + offset);
            if (offset + drec->hdr.size > jhdr->nbytes_used) {
                fprintf(stderr, "Warning: Incomplete DATA record at offset %u, skipping\n", offset);
                break;
            }
            offset += drec->hdr.size;

        } else if (rhdr->type == REC_COMMIT) {
            struct commit_record *crec = (struct commit_record *)(journal_area + offset);
            if (offset + crec->hdr.size > jhdr->nbytes_used) {
                fprintf(stderr, "Warning: Incomplete COMMIT record at offset %u, skipping\n", offset);
                break;
            }

            uint32_t replay_offset = sizeof(struct journal_header);
            if (transaction_count > 0) {
                int txn = 0;
                replay_offset = sizeof(struct journal_header);
                while (txn < transaction_count && replay_offset < offset) {
                    struct rec_header *r = (struct rec_header *)(journal_area + replay_offset);
                    if (r->type == REC_COMMIT) {
                        txn++;
                        replay_offset += r->size;
                    } else {
                        replay_offset += r->size;
                    }
                }
            }

            while (replay_offset < offset) {
                struct rec_header *r = (struct rec_header *)(journal_area + replay_offset);
                if (r->type == REC_DATA) {
                    struct data_record *d = (struct data_record *)(journal_area + replay_offset);
                    pwrite_block(fd, d->block_no, d->data);
                }
                replay_offset += r->size;
            }

            transaction_count++;
            offset += crec->hdr.size;

        } else {
            fprintf(stderr, "Warning: Unknown record type %u at offset %u\n", rhdr->type, offset);
            break;
        }
    }

    jhdr->magic = JOURNAL_MAGIC;
    jhdr->nbytes_used = sizeof(struct journal_header);
    w_journal_a(fd, journal_area);

    printf("Installed %d committed transactions from journal.\n", transaction_count);
}


int main(int argc, char *argv[]) {
    if (argc < 2) {
        fprintf(stderr, "Usage:\n");
        fprintf(stderr, "  %s create <filename>\n", argv[0]);
        fprintf(stderr, "  %s install\n", argv[0]);
        exit(EXIT_FAILURE);
    }

    const char *command = argv[1];
    const char *image_path = DEFAULT_IMAGE;

    int fd = open(image_path, O_RDWR);
    if (fd < 0) {
        die("open");
    }

    if (strcmp(command, "create") == 0) {
        if (argc != 3) {
            fprintf(stderr, "Error: create requires a filename\n");
            exit(EXIT_FAILURE);
        }
        create_c(fd, argv[2]);
    } else if (strcmp(command, "install") == 0) {
        install_c(fd);
    } else {
        fprintf(stderr, "Error: Unknown command '%s'\n", command);
    }

    if (close(fd) < 0) {
        die("close");
    }

    return 0;
}