cat << 'INNER_EOF' >> src/syscall.c

struct open_file {
    uint8_t used;
    const uint8_t* ptr;
    size_t size;
    size_t offset;
};
static struct open_file open_files[16] = {0};
INNER_EOF
