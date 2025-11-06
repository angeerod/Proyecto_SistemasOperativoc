#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <errno.h>

#define MAX_PATH_DIRS 256
#define MAX_PARALLEL_CMDS 64
#define PROMPT "gtesh> "
#define ERROR_MSG "An error has occurred\n"

// Estructura para un comando individual
typedef struct {
    char **argv;           // NULL-terminated array de argumentos
    char *redir_file;      // Archivo de redirección (NULL si no hay)
    int is_background;     // 1 si es parte de una cadena de comandos paralelos
} cmd_t;

// Variables globales para el PATH
static char **path_dirs = NULL;
static size_t path_count = 0;

// Función de utilidad para imprimir error
static void util_print_error(void) {
    write(STDERR_FILENO, ERROR_MSG, strlen(ERROR_MSG));
}

// Liberar memoria de un comando
static void free_command(cmd_t *cmd) {
    if (!cmd) return;
    if (cmd->argv) {
        for (int i = 0; cmd->argv[i]; i++) {
            free(cmd->argv[i]);
        }
        free(cmd->argv);
    }
    free(cmd->redir_file);
    free(cmd);
}

// Inicializar PATH con /bin
static void init_path(void) {
    path_dirs = malloc(sizeof(char*));
    if (!path_dirs) {
        util_print_error();
        exit(1);
    }
    path_dirs[0] = strdup("/bin");
    if (!path_dirs[0]) {
        free(path_dirs);
        util_print_error();
        exit(1);
    }
    path_count = 1;
}

// Actualizar PATH con nuevos directorios
static void update_path(char **new_dirs, size_t count) {
    for (size_t i = 0; i < path_count; i++) {
        free(path_dirs[i]);
    }
    free(path_dirs);
    
    if (count == 0) {
        path_dirs = NULL;
        path_count = 0;
        return;
    }

    path_dirs = malloc(count * sizeof(char*));
    if (!path_dirs) {
        util_print_error();
        exit(1);
    }

    for (size_t i = 0; i < count; i++) {
        path_dirs[i] = strdup(new_dirs[i]);
        if (!path_dirs[i]) {
            for (size_t j = 0; j < i; j++) {
                free(path_dirs[j]);
            }
            free(path_dirs);
            util_print_error();
            exit(1);
        }
    }
    path_count = count;
}

// Buscar un ejecutable en el PATH
static char *find_executable(const char *cmd) {
    if (strchr(cmd, '/')) {
        // Si el comando incluye '/', usarlo directamente
        if (access(cmd, X_OK) == 0) {
            return strdup(cmd);
        }
        return NULL;
    }

    for (size_t i = 0; i < path_count; i++) {
        char *full_path = NULL;
        if (asprintf(&full_path, "%s/%s", path_dirs[i], cmd) == -1) {
            util_print_error();
            continue;
        }
        if (access(full_path, X_OK) == 0) {
            return full_path;
        }
        free(full_path);
    }
    return NULL;
}

// Manejar comandos builtin
static int handle_builtin(cmd_t *cmd) {
    if (!cmd || !cmd->argv || !cmd->argv[0]) return 0;

    if (strcmp(cmd->argv[0], "exit") == 0) {
        if (cmd->argv[1]) {
            util_print_error();
            return 1;
        }
        exit(0);
    }

    if (strcmp(cmd->argv[0], "cd") == 0) {
        if (!cmd->argv[1] || cmd->argv[2]) {
            util_print_error();
            return 1;
        }
        if (chdir(cmd->argv[1]) != 0) {
            util_print_error();
        }
        return 1;
    }

    if (strcmp(cmd->argv[0], "path") == 0) {
        // Contar argumentos
        int count = 0;
        while (cmd->argv[count + 1]) count++;
        update_path(cmd->argv + 1, count);
        return 1;
    }

    return 0;
}

// Parsear una línea en un comando
static cmd_t *parse_command(char *line) {
    cmd_t *cmd = calloc(1, sizeof(cmd_t));
    if (!cmd) {
        util_print_error();
        return NULL;
    }

    // Primero separar por redirección
    char *cmd_part = line;
    char *redir_part = NULL;
    char *gt_pos = strchr(line, '>');
    
    if (gt_pos) {
        *gt_pos = '\0';
        redir_part = gt_pos + 1;
        while (*redir_part == ' ' || *redir_part == '\t') redir_part++;
        
        // Verificar que solo hay un archivo después de >
        char *next_token = strchr(redir_part, ' ');
        char *next_gt = strchr(redir_part, '>');
        if (!*redir_part || next_gt || (next_token && *(next_token+1) != '\0')) {
            util_print_error();
            free_command(cmd);
            return NULL;
        }
        cmd->redir_file = strdup(redir_part);
        if (!cmd->redir_file) {
            util_print_error();
            free_command(cmd);
            return NULL;
        }
        // Eliminar espacios al final del nombre del archivo
        char *end = cmd->redir_file + strlen(cmd->redir_file) - 1;
        while (end > cmd->redir_file && (*end == ' ' || *end == '\t')) {
            *end = '\0';
            end--;
        }
    }

    // Contar tokens y alocar argv
    int token_count = 0;
    char *tmp = strdup(cmd_part);
    if (!tmp) {
        util_print_error();
        free_command(cmd);
        return NULL;
    }
    char *token = strtok(tmp, " \t");
    while (token) {
        token_count++;
        token = strtok(NULL, " \t");
    }
    free(tmp);

    if (token_count == 0) {
        free_command(cmd);
        return NULL;
    }

    cmd->argv = calloc(token_count + 1, sizeof(char*));
    if (!cmd->argv) {
        util_print_error();
        free_command(cmd);
        return NULL;
    }

    // Llenar argv
    int i = 0;
    token = strtok(cmd_part, " \t");
    while (token && i < token_count) {
        cmd->argv[i] = strdup(token);
        if (!cmd->argv[i]) {
            util_print_error();
            free_command(cmd);
            return NULL;
        }
        i++;
        token = strtok(NULL, " \t");
    }
    cmd->argv[i] = NULL;

    return cmd;
}

// Dividir una línea en comandos paralelos
static cmd_t **split_parallel_commands(char *line, int *count) {
    *count = 0;
    cmd_t **cmds = malloc(MAX_PARALLEL_CMDS * sizeof(cmd_t*));
    if (!cmds) {
        util_print_error();
        return NULL;
    }

    char *saveptr;
    char *token = strtok_r(line, "&", &saveptr);
    
    while (token && *count < MAX_PARALLEL_CMDS) {
        // Eliminar espacios al inicio y final
        while (*token == ' ' || *token == '\t') token++;
        char *end = token + strlen(token) - 1;
        while (end > token && (*end == ' ' || *end == '\t')) {
            *end = '\0';
            end--;
        }
        
        if (*token) {  // Ignorar tokens vacíos
            cmd_t *cmd = parse_command(token);
            if (cmd) {
                cmd->is_background = 1;
                cmds[*count] = cmd;
                (*count)++;
            }
        }
        
        token = strtok_r(NULL, "&", &saveptr);
    }

    if (*count == 0) {
        free(cmds);
        return NULL;
    }

    // El último comando no es background a menos que haya un & al final
    if (*count > 0) {
        cmds[*count - 1]->is_background = (line[strlen(line) - 1] == '&');
    }

    return cmds;
}

// Ejecutar un comando
static int execute_command(cmd_t *cmd) {
    if (!cmd || !cmd->argv || !cmd->argv[0]) return -1;

    // Manejar builtins primero
    if (handle_builtin(cmd)) {
        return 0;
    }

    // Buscar el ejecutable
    char *exec_path = find_executable(cmd->argv[0]);
    if (!exec_path) {
        util_print_error();
        return -1;
    }

    pid_t pid = fork();
    if (pid == -1) {
        free(exec_path);
        util_print_error();
        return -1;
    }

    if (pid == 0) {  // Proceso hijo
        // Configurar redirección si es necesario
        if (cmd->redir_file) {
            int fd = open(cmd->redir_file, O_WRONLY | O_CREAT | O_TRUNC, 0644);
            if (fd == -1) {
                util_print_error();
                exit(1);
            }
            if (dup2(fd, STDOUT_FILENO) == -1 || dup2(fd, STDERR_FILENO) == -1) {
                util_print_error();
                close(fd);
                exit(1);
            }
            close(fd);
        }

        // Ejecutar el comando
        execv(exec_path, cmd->argv);
        util_print_error();  // Solo llegamos aquí si execv falla
        exit(1);
    }

    free(exec_path);
    if (!cmd->is_background) {
        int status;
        waitpid(pid, &status, 0);
    }
    return pid;
}

// Esperar a que terminen todos los procesos en background
static void wait_for_children(void) {
    while (waitpid(-1, NULL, 0) > 0);
}

int main(int argc, char *argv[]) {
    // Verificar argumentos
    if (argc > 2) {
        util_print_error();
        exit(1);
    }

    // Inicializar PATH
    init_path();

    // Modo batch
    if (argc == 2) {
        FILE *batch = fopen(argv[1], "r");
        if (!batch) {
            util_print_error();
            exit(1);
        }

        char *line = NULL;
        size_t len = 0;
        ssize_t read;

        while ((read = getline(&line, &len, batch)) != -1) {
            if (read > 0 && line[read-1] == '\n') {
                line[read-1] = '\0';
            }

            int cmd_count;
            cmd_t **cmds = split_parallel_commands(line, &cmd_count);
            if (cmds) {
                for (int i = 0; i < cmd_count; i++) {
                    if (execute_command(cmds[i]) == -1) {
                        // Continuar después de errores en modo batch
                        continue;
                    }
                }
                // Esperar todos los comandos paralelos
                wait_for_children();
                
                // Liberar memoria
                for (int i = 0; i < cmd_count; i++) {
                    free_command(cmds[i]);
                }
                free(cmds);
            }
        }

        free(line);
        fclose(batch);
        exit(0);
    }

    // Modo interactivo
    char *line = NULL;
    size_t len = 0;
    ssize_t read;

    while (1) {
        printf("%s", PROMPT);
        fflush(stdout);

        read = getline(&line, &len, stdin);
        if (read == -1) {
            break;  // EOF
        }

        if (read > 0 && line[read-1] == '\n') {
            line[read-1] = '\0';
        }

        int cmd_count;
        cmd_t **cmds = split_parallel_commands(line, &cmd_count);
        if (cmds) {
            for (int i = 0; i < cmd_count; i++) {
                if (execute_command(cmds[i]) == -1) {
                    // Continuar después de errores en modo interactivo
                    continue;
                }
            }
            // Esperar todos los comandos paralelos
            wait_for_children();
            
            // Liberar memoria
            for (int i = 0; i < cmd_count; i++) {
                free_command(cmds[i]);
            }
            free(cmds);
        }
    }

    free(line);
    exit(0);
}
