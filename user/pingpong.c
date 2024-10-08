#include "kernel/types.h"
#include "user.h"

int main(int argc, char* argv[]){
    int c2f[2], f2c[2];
    char buffer_data[10];
    int buffer_id;
    // 创建两个管道
    pipe(c2f);  // c2f[0] 是读端, c2f[1] 是写端
    pipe(f2c);  // f2c[0] 是读端, f2c[1] 是写端
    if(fork() == 0){
        /* 子进程读出 */
        close(f2c[1]);  // 关闭f2c的写端，子进程只需要读
        close(c2f[0]);  // 关闭c2f的读端，子进程只需要写
        // 从父进程读取 "ping" 和 pid
        read(f2c[0], buffer_data, sizeof(buffer_data));
        read(f2c[0], &buffer_id, sizeof(buffer_id));
        printf("%d: received %s from pid %d\n", getpid(), buffer_data, buffer_id);
        
        // 向父进程发送"pong"和子进程pid
        write(c2f[1], "pong", sizeof(buffer_data));
        int pid = getpid();
        write(c2f[1], &pid, sizeof(buffer_id));

        close(f2c[0]);  // 关闭读端
        close(c2f[1]);  // 关闭写端
        exit(0);    // 子进程退出
    }else{
        /* 父进程写入数据 */
        close(f2c[0]);  // 关闭f2c的读端，父进程只需要写
        close(c2f[1]);  // 关闭c2f的写端，父进程只需要读

        // 向子进程发送 "ping" 和父进程 pid
        write(f2c[1], "ping", sizeof(buffer_data));
        int pid = getpid();
        write(f2c[1], &pid, sizeof(buffer_id));
        close(f2c[1]);
        wait(0);    // 父进程阻塞，等待子进程读取

        // 从子进程读取 "pong" 和 pid
        read(c2f[0], buffer_data, 10);
        read(c2f[0], &buffer_id, 5);
        printf("%d: received %s from pid %d\n", getpid(), buffer_data, buffer_id);
        
        close(c2f[0]);  // 关闭c2f的读端
        exit(0);    // 父进程一定要退出
    }
}