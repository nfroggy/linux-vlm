/* Regression for saving over a world that still backs live private mappings. */
#include "std.h"
#include "world_tools.h"
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

Boolean Trace = FALSE;
Boolean EnableIDS = FALSE;
Boolean TestFunction = FALSE;

#define CHECK(condition) do { \
    if (!(condition)) { \
        fprintf(stderr, "%s:%d: %s (errno %d)\n", __FILE__, __LINE__, #condition, errno); \
        exit(1); \
    } \
} while (0)

enum { imageSize = 65536 };

static void checkImage(const char *path, const unsigned char *expected)
{
    unsigned char actual[imageSize];
    int fd = open(path, O_RDONLY);
    CHECK(fd >= 0);
    CHECK(read(fd, actual, sizeof(actual)) == sizeof(actual));
    CHECK(memcmp(actual, expected, sizeof(actual)) == 0);
    CHECK(read(fd, actual, 1) == 0);
    CHECK(close(fd) == 0);
}

static void checkNoTemporaryFiles(const char *path)
{
    DIR *dir = opendir(path);
    struct dirent *entry;
    int count = 0;
    CHECK(dir != NULL);
    while ((entry = readdir(dir)) != NULL) {
        if (!strcmp(entry->d_name, ".") || !strcmp(entry->d_name, ".."))
            continue;
        CHECK(strcmp(entry->d_name, "world.vlod") == 0);
        count++;
    }
    CHECK(count == 1);
    CHECK(closedir(dir) == 0);
}

int main(void)
{
    char directory[] = "/tmp/vlm-world-save-XXXXXX";
    char pathname[sizeof(directory) + sizeof("/world.vlod")];
    unsigned char original[imageSize], expected[imageSize];
    unsigned char *mapped;
    struct stat before, after;
    World world = {0};
    int fd, status;
    pid_t child;

    CHECK(mkdtemp(directory) != NULL);
    snprintf(pathname, sizeof(pathname), "%s/world.vlod", directory);
    for (size_t i = 0; i < sizeof(original); i++)
        original[i] = (unsigned char)(i * 17 + 43);
    memcpy(expected, original, sizeof(expected));
    expected[0] ^= 0xff;
    fd = open(pathname, O_RDWR | O_CREAT | O_EXCL, 0600);
    CHECK(fd >= 0);
    CHECK(fchmod(fd, 0640) == 0);
    CHECK(write(fd, original, sizeof(original)) == sizeof(original));
    CHECK(fstat(fd, &before) == 0);
    mapped = mmap(NULL, sizeof(original), PROT_READ | PROT_WRITE, MAP_PRIVATE, fd, 0);
    CHECK(mapped != MAP_FAILED);
    CHECK(close(fd) == 0);
    mapped[0] ^= 0xff; /* One dirty page; the remaining pages still depend on the file. */

    world.pathname = pathname;
    world.format = VLMWorldFormat;
    CreateWorldFile(&world);
    /* The old O_TRUNC implementation fails here, before accessing invalid mappings. */
    checkImage(pathname, original);
    CHECK(write(world.fd, mapped, sizeof(original)) == sizeof(original));
    checkImage(pathname, original); /* A partial/pending save is never visible. */
    CommitWorldFile(&world);
    CloseWorldFile(&world, TRUE);
    CHECK(stat(pathname, &after) == 0);
    CHECK(before.st_ino != after.st_ino);
    CHECK((after.st_mode & 0777) == 0640);
    checkImage(pathname, expected);
    CHECK(memcmp(mapped, expected, sizeof(expected)) == 0);
    CHECK(munmap(mapped, sizeof(original)) == 0);
    checkNoTemporaryFiles(directory);

    CreateWorldFile(&world);
    CHECK(write(world.fd, "unfinished", 10) == 10);
    CloseWorldFile(&world, TRUE);
    checkImage(pathname, expected);
    checkNoTemporaryFiles(directory);

    /* A commit error must leave the previous world intact and remove the staging file. */
    child = fork();
    CHECK(child >= 0);
    if (child == 0) {
        CreateWorldFile(&world);
        CHECK(write(world.fd, "unfinished", 10) == 10);
        CHECK(close(world.fd) == 0);
        world.fd = -1; /* Force fsync to fail with EBADF. */
        CommitWorldFile(&world);
        _exit(0);
    }
    CHECK(waitpid(child, &status, 0) == child);
    CHECK(WIFEXITED(status) && WEXITSTATUS(status) != 0);
    checkImage(pathname, expected);
    checkNoTemporaryFiles(directory);

    CHECK(unlink(pathname) == 0);
    CHECK(rmdir(directory) == 0);
    puts("PASS: mapped world replacement, permissions, aborted save, failed commit");
    return 0;
}
