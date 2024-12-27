#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <fcntl.h>
#include <signal.h>
#include <sys/wait.h>

#define MAX_CMD_LEN 1024 // Maksimum komut uzunluğu
#define MAX_ARGS 64 // Maksimum argüman sayısı

void prompt() {
    printf("> "); // Kullanıcıdan komut istemi
    fflush(stdout); // Çıkış akışını temizle
}

void handle_sigchld(int sig) {
    int status;
    pid_t pid;
    while ((pid = waitpid(-1, &status, WNOHANG)) > 0) { // Çocuk süreçlerin durumunu kontrol et
        printf("[%d] retval: %d\n", pid, WEXITSTATUS(status)); // Çocuk sürecin çıkış durumunu yazdır
        prompt(); // Komut istemini tekrar göster
    }
}

void execute_single_command(char *cmd) {
    char *args[MAX_ARGS]; // Argümanları tutacak dizi
    char *token = strtok(cmd, " "); // Komutu boşluklara göre parçala
    int i = 0;
    int background = 0; // Arka planda çalıştırma bayrağı
    int input_redirect = 0; // Girdi yönlendirme bayrağı
    int output_redirect = 0; // Çıktı yönlendirme bayrağı
    int pipe_present = 0; // Pipe var mı kontrolü
    char *input_file = NULL; // Girdi dosyası
    char *output_file = NULL; // Çıktı dosyası
    char *pipe_cmds[MAX_ARGS] = {NULL}; // Pipe komutlarını tutacak dizi

    while (token != NULL) {
        if (strcmp(token, "&") == 0) {
            background = 1; // Arka planda çalıştırma
        } else if (strcmp(token, "<") == 0) {
            input_redirect = 1; // Girdi yönlendirme
            token = strtok(NULL, " ");
            input_file = token; // Girdi dosyasını al
        } else if (strcmp(token, ">") == 0) {
            output_redirect = 1; // Çıktı yönlendirme
            token = strtok(NULL, " ");
            output_file = token; // Çıktı dosyasını al
        } else if (strcmp(token, "|") == 0) {
            pipe_present = 1; // Pipe var
            token = strtok(NULL, ""); // Pipe sonrası komutu al
            pipe_cmds[i++] = token; // Pipe komutunu diziye ekle
            break;
        } else {
            args[i++] = token; // Argümanı diziye ekle
        }
        token = strtok(NULL, " ");
    }
    args[i] = NULL; // Argüman dizisinin sonunu belirt

    if (pipe_present) {
        int num_pipes = 0;
        for (int j = 0; pipe_cmds[j] != NULL; j++) {
            num_pipes++; // Pipe sayısını hesapla
        }

        int pipefds[2 * num_pipes]; // Pipe dosya tanıtıcıları
        for (int j = 0; j < num_pipes; j++) {
            if (pipe(pipefds + j * 2) < 0) { // Pipe oluştur
                perror("Pipe failed");
                exit(EXIT_FAILURE);
            }
        }

        int pid;
        int cmd_index = 0;
        while (cmd_index <= num_pipes) {
            pid = fork(); // Yeni süreç oluştur
            if (pid == 0) {
                if (cmd_index != 0) {
                    if (dup2(pipefds[(cmd_index - 1) * 2], 0) < 0) { // Girdi yönlendirme
                        perror("dup2 failed");
                        exit(EXIT_FAILURE);
                    }
                }
                if (cmd_index != num_pipes) {
                    if (dup2(pipefds[cmd_index * 2 + 1], 1) < 0) { // Çıktı yönlendirme
                        perror("dup2 failed");
                        exit(EXIT_FAILURE);
                    }
                }

                for (int j = 0; j < 2 * num_pipes; j++) {
                    close(pipefds[j]); // Pipe dosya tanıtıcılarını kapat
                }

                char *cmd_args[MAX_ARGS];
                int k = 0;
                token = strtok(cmd_index == 0 ? cmd : pipe_cmds[cmd_index - 1], " ");
                while (token != NULL) {
                    cmd_args[k++] = token; // Komut argümanlarını al
                    token = strtok(NULL, " ");
                }
                cmd_args[k] = NULL;

                execvp(cmd_args[0], cmd_args); // Komutu çalıştır
                perror("execvp failed");
                exit(EXIT_FAILURE);
            } else if (pid < 0) {
                perror("Fork failed");
                exit(EXIT_FAILURE);
            }
            cmd_index++;
        }

        for (int j = 0; j < 2 * num_pipes; j++) {
            close(pipefds[j]); // Pipe dosya tanıtıcılarını kapat
        }

        for (int j = 0; j <= num_pipes; j++) {
            wait(NULL); // Çocuk süreçlerin bitmesini bekle
        }
    } else {
        pid_t pid = fork(); // Yeni süreç oluştur
        if (pid == 0) {
            if (input_redirect) {
                int fd = open(input_file, O_RDONLY); // Girdi dosyasını aç
                if (fd < 0) {
                    perror("Girdi dosyasi bulunamadi");
                    exit(1);
                }
                dup2(fd, STDIN_FILENO); // Girdi yönlendirme
                close(fd);
            }
            if (output_redirect) {
                int fd = open(output_file, O_WRONLY | O_CREAT | O_TRUNC, 0644); // Çıktı dosyasını aç
                dup2(fd, STDOUT_FILENO); // Çıktı yönlendirme
                close(fd);
            }
            execvp(args[0], args); // Komutu çalıştır
            perror("Komut yurutme basarisiz oldu");
            exit(1);
        } else if (pid > 0) {
            if (!background) {
                waitpid(pid, NULL, 0); // Çocuk sürecin bitmesini bekle
            } else {
                printf("[%d]\n", pid); // Arka planda çalışan sürecin PID'sini yazdır
            }
        } else {
            perror("Fork failed");
        }
    }
}

void execute_command(char *cmd) {
    char *commands[MAX_ARGS]; // Komutları tutacak dizi
    char *token = strtok(cmd, ";"); // Komutları ';' ile ayır
    int i = 0;

    while (token != NULL) {
        commands[i++] = token; // Komutu diziye ekle
        token = strtok(NULL, ";");
    }
    commands[i] = NULL; // Komut dizisinin sonunu belirt

    for (int j = 0; commands[j] != NULL; j++) {
        execute_single_command(commands[j]); // Her bir komutu çalıştır
    }
}

int main() {
    signal(SIGCHLD, handle_sigchld); // SIGCHLD sinyalini yakala
    char cmd[MAX_CMD_LEN];

    while (1) {
        prompt(); // Komut istemini göster
        if (fgets(cmd, sizeof(cmd), stdin) == NULL) {
            break; // Kullanıcıdan komut al
        }
        cmd[strcspn(cmd, "\n")] = 0; // Yeni satır karakterini kaldır
        if (strcmp(cmd, "quit") == 0) {
            break; // 'quit' komutu girildiyse döngüden çık
        }
        execute_command(cmd); // Komutu çalıştır
    }

    return 0;
}