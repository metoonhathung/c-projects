#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#include <signal.h>

typedef struct Node {
    int jid;
    int pid;
    int is_running;
    int is_bg;
    char* command;
    struct Node* next;
} Node;

Node* fake_head;
volatile sig_atomic_t pid;
int jid;
int is_bg;
char** tokens;

void add_job() {
    Node* new_node = malloc(sizeof(Node));
    new_node->jid = jid;
    new_node->pid = pid;
    new_node->is_running = 1;
    new_node->is_bg = is_bg;
    new_node->command = malloc(128 * sizeof(char));
    new_node->command[0] = '\0';
    strcat(new_node->command, tokens[0]);
    int i = 1;
    while (tokens[i] != NULL) {
        strcat(new_node->command, " ");
        strcat(new_node->command, tokens[i]);
        i++;
    }
    new_node->next = NULL;
    new_node->next = fake_head->next;
    fake_head->next = new_node;
}

void delete_job(int pid) {
    Node* curr = fake_head;
    while (curr->next != NULL) {
        if (curr->next->pid != pid) {
            curr = curr->next;
        } else {
            Node* temp = curr->next;
            curr->next = curr->next->next;
            temp->next = NULL;
            free(temp->command);
            free(temp);
            return;
        }
    }
}

Node* find_job(int jid) {
    Node* curr = fake_head;
    while (curr->next != NULL) {
        if (curr->next->jid != jid) {
            curr = curr->next;
        } else {
            return curr->next;
        }
    }
    return NULL;
}

void print_ll(Node* head) {
    if (head == NULL)
        return;
    print_ll(head->next);
    printf("[%d] %d %s %s", head->jid, head->pid, head->is_running ? "Running" : "Stopped", head->command);
    if (head->is_bg)
        printf(" %c", '&');
    printf("\n");
}

void free_ll() {
    while (fake_head != NULL) {
        Node* temp = fake_head->next;
        free(fake_head->command);
        free(fake_head);
        fake_head = temp;
    }
}

void builtin_exit(char** argv) {
    Node* curr = fake_head;
    while (curr->next != NULL) {
        kill(-curr->next->pid, SIGHUP);
        if (!curr->next->is_running)
            kill(-curr->next->pid, SIGCONT);
        curr = curr->next;
    }
    exit(0);
}

void builtin_fg(char** argv) {
    jid = atoi(argv[1] + 1);
    Node* job = find_job(jid);
    job->is_running = 1;
    job->is_bg = 0;
    sigset_t mask_all, prev_all;
    sigfillset(&mask_all);
    sigprocmask(SIG_BLOCK, &mask_all, &prev_all); /* Block SIGCHLD */
    kill(-job->pid, SIGCONT);
    pid = 0;
    while (pid != job->pid)
        sigsuspend(&prev_all);
    sigprocmask(SIG_SETMASK, &prev_all, NULL); /* Unblock SIGCHLD */
}

void builtin_bg(char** argv) {
    jid = atoi(argv[1] + 1);
    Node* job = find_job(jid);
    job->is_running = 1;
    job->is_bg = 1;
    kill(-job->pid, SIGCONT);
}

void builtin_jobs(char** argv) {
    print_ll(fake_head->next);
}

void builtin_kill(char** argv) {
    jid = atoi(argv[1] + 1);
    Node* job = find_job(jid);
    kill(-job->pid, SIGTERM);
}

void builtin_cd(char** argv) {
    char* path = argv[1] == NULL ? getenv("HOME") : argv[1];
    if (chdir(path) == 0) {
        char* curr_dir = getcwd(NULL, 0);
        setenv("PWD", curr_dir, 1);
        free(curr_dir);
    } else {
        perror(argv[1]);
    }
}

struct builtin {
    char* name;
    void (*fun_ptr)(char**);
};

struct builtin builtins[] = {
    {"exit", builtin_exit},
    {"cd", builtin_cd},
    {"fg", builtin_fg},
    {"bg", builtin_bg},
    {"jobs", builtin_jobs},
    {"kill", builtin_kill},
};

void sigchld_handler(int sig) {
    int olderrno = errno;
    sigset_t mask_all, prev_all;
    sigfillset(&mask_all);
    int status;
    pid = waitpid(-1, &status, WUNTRACED | WNOHANG); /* Reap child */
    sigprocmask(SIG_BLOCK, &mask_all, &prev_all);
    Node* last_job = fake_head->next;
    if (WIFEXITED(status)) {
        delete_job(pid); /* Delete the child from the job list */
    } else if (WIFSIGNALED(status)) {
        printf("[%d] %d terminated by signal %d\n", last_job->jid, last_job->pid, status);
        delete_job(pid); /* Delete the child from the job list */
    } else if (WIFSTOPPED(status)) {
        last_job->is_running = 0;
    }
    sigprocmask(SIG_SETMASK, &prev_all, NULL);
    errno = olderrno;
}

void sigint_handler(int sig) {
    Node* last_job = fake_head->next;
    if (last_job && !last_job->is_bg)
        kill(-last_job->pid, SIGINT);
}

void sigtstp_handler(int sig) {
    Node* last_job = fake_head->next;
    if (last_job && !last_job->is_bg)
        kill(-last_job->pid, SIGTSTP);
}

void execute(char** argv) {
    for (int i = 0; i < sizeof(builtins) / sizeof(struct builtin); i++) {
        if (strcmp(argv[0], builtins[i].name) == 0) {
            builtins[i].fun_ptr(argv);
            return;
        }
    }
    sigset_t mask_all, prev_all;
    sigfillset(&mask_all);
    sigprocmask(SIG_BLOCK, &mask_all, &prev_all); /* Block SIGCHLD */
    if ((pid = fork()) == 0) { /* Child process */
        signal(SIGINT, SIG_DFL);
        signal(SIGTSTP, SIG_DFL);
        sigprocmask(SIG_SETMASK, &prev_all, NULL); /* Unblock SIGCHLD */
        setpgid(0, 0);
        execvp(argv[0], argv);
        printf("%s: command not found\n", argv[0]);
        exit(1);
    }
    add_job(); /* Add the child to the job list */
    if (is_bg) {
        printf("[%d] %d\n", jid, pid);
    } else { /* Wait for SIGCHLD to be received */
        int curr_pid = pid;
        pid = 0;
        while (pid != curr_pid)
            sigsuspend(&prev_all);
    }
    sigprocmask(SIG_SETMASK, &prev_all, NULL); /* Unblock SIGCHLD */
    /* Do some work after receiving SIGCHLD */
}

char* read_line() {
    char* line = NULL;
    size_t size = 0;
    ssize_t line_size = getline(&line, &size, stdin);
    if (line_size == -1)
        builtin_exit(NULL);
    return line;
}

char** split_line(char* line) {
    if (strchr(line, '&'))
        is_bg = 1;
    int size = 16;
    char** tokens = malloc(size * sizeof(char*));
    char* sep = " \t\r\n\a";
    char* token = strtok(line, sep);
    int i = 0;
    while (token != NULL) {
        tokens[i] = token;
        i++;
        if (i >= size) {
            size *= 2;
            tokens = realloc(tokens, size * sizeof(char*));
        }
        token = strtok(NULL, sep);
    }
    tokens[i - is_bg] = NULL;
    return tokens;
}

int main(int argc, char** argv) {
    signal(SIGCHLD, sigchld_handler);
    signal(SIGINT, sigint_handler);
    signal(SIGTSTP, sigtstp_handler);
    fake_head = malloc(sizeof(Node)); /* Initialize the job list */
    fake_head->next = NULL;
    jid = 0;
    while (1) {
        printf("> ");
        is_bg = 0;
        jid++;
        char* line = read_line();
        tokens = split_line(line);
        if (tokens[0] != NULL)
            execute(tokens);
        free(line);
        free(tokens);
    }
    free_ll();
    return 0;
}
