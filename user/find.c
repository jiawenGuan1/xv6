#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"
#include "kernel/fs.h"


// fmtname的作用就是提取文件名，如输入./a/target将输出target
char *fmtname(char *path) {
    static char buf[DIRSIZ + 1];
    char *p;

    // 找到最后一个斜杠后的第一个字符
    for (p = path + strlen(path); p >= path && *p != '/'; p--)
        ;
    p++;

    // 返回填充空格的名称
    if (strlen(p) >= DIRSIZ) return p;
    memmove(buf, p, strlen(p));
    memset(buf + strlen(p), ' ', DIRSIZ - strlen(p));
    buf[strlen(p)] = 0;     // 确保strcmp(fmtname(path), filename)能够顺利进行比较
    return buf;
}

// 在目录树中查找名称与字符串filename匹配的所有文件或目录，输出文件的相对路径
void find(char *path, char* filename) {
    char buf[512], *p;
    int fd;
    struct dirent de;
    struct stat st;

    if ((fd = open(path, 0)) < 0) {
        fprintf(2, "find: cannot open %s\n", path);
        return;
    }

    if (fstat(fd, &st) < 0) {
        fprintf(2, "find: cannot stat %s\n", path);
        close(fd);
        return;
    }

    switch (st.type) {
        case T_FILE:
            if (strcmp(fmtname(path), filename) == 0) {
                printf("%s\n", path);
            }
            break;

        case T_DIR:
            if (strcmp(fmtname(path), filename) == 0) {
                printf("%s\n", path);
            }
            if (strlen(path) + 1 + DIRSIZ + 1 > sizeof buf) {
                printf("find: path too long\n");
                break;
            }
            strcpy(buf, path);
            p = buf + strlen(buf);
            *p++ = '/';

            // 递归调用find进入子目录，但不允许递归进入.和..
            while (read(fd, &de, sizeof(de)) == sizeof(de)) {
                if (de.inum == 0 || strcmp(de.name, ".") == 0 || strcmp(de.name, "..") == 0) continue;
                memmove(p, de.name, DIRSIZ);    // 在buf末尾加上文件名
                p[DIRSIZ] = 0;

                find(buf, filename);
            }
            break;
    }
    close(fd);
}

int main(int argc, char *argv[]) {

    if (argc != 3) {
        printf("error\n");
        exit(-1);
    }
    find(argv[1], argv[2]);
    exit(0);
}