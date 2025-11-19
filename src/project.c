// _GNU_SOURCE: Habilita extensiones GNU (asprintf, getline, etc.)
#define _GNU_SOURCE
#include <stdio.h>   
#include <stdlib.h>     // malloc, free, exit, calloc
#include <string.h>     // strlen, strcmp, strchr, strtok, strdup
#include <unistd.h>     // fork, execv, chdir, access, write
#include <sys/wait.h>   // wait, waitpid
#include <sys/types.h>  // pid_t, size_t
#include <sys/stat.h>   
#include <fcntl.h>      // open, O_WRONLY, O_CREAT, O_TRUNC
#include <errno.h>
#include <readline/readline.h>  // readline: edición interactiva de línea
#include <readline/history.h>   // add_history: historial de comandos      

// Constantes del programa
#define MAX_PATH_DIRS 256       // Máximo número de directorios en PATH
#define MAX_PARALLEL_CMDS 64    // Máximo número de comandos paralelos con &
#define PROMPT "\033[35mgtesh>\033[0m "        // Prompt en morado
#define ERROR_MSG "\033[31mAn error has occurred\033[0m\n"  // Mensaje de error en rojo

// Estructura para representar un comando individual
typedef struct {
    char **argv;           // Array de argumentos (NULL-terminated), ej: ["ls", "-la", NULL]
    char *redir_file;      // Nombre del archivo de redirección (NULL si no hay >)
    int is_background;     // 1 si el comando es parte de una cadena paralela (&)
} cmd_t;

// Variables globales para el PATH del shell
// path_dirs: Array dinámico de strings, cada uno es un directorio
// path_count: Número de directorios actuales en el PATH
static char **path_dirs = NULL;
static size_t path_count = 0;

// Función de utilidad para imprimir el mensaje de error único
// Escribe en stderr (file descriptor 2)
static void util_print_error(void) {
    write(STDERR_FILENO, ERROR_MSG, strlen(ERROR_MSG));
}

// Liberar toda la memoria asociada a un comando
// Libera cada string en argv, el array argv, el nombre de archivo de redirección
// y finalmente la estructura cmd_t misma
static void free_command(cmd_t *cmd) {
    if (!cmd) return;  // Si es NULL, no hay nada que liberar
    if (cmd->argv) {
        // Liberar cada argumento individualmente
        for (int i = 0; cmd->argv[i]; i++) {
            free(cmd->argv[i]);
        }
        free(cmd->argv);  // Liberar el array de punteros
    }
    free(cmd->redir_file);  // Liberar el nombre del archivo (puede ser NULL)
    free(cmd);  // Liberar la estructura principal
}

// Inicializar PATH con /bin al arrancar el shell
// El PATH inicial debe contener el directorio /bin
static void init_path(void) {
    path_dirs = malloc(sizeof(char*));  // Alocar espacio para 1 puntero
    if (!path_dirs) {
        util_print_error();
        exit(1);
    }
    path_dirs[0] = strdup("/bin");  // Copiar la string "/bin"
    if (!path_dirs[0]) {
        free(path_dirs);
        util_print_error();
        exit(1);
    }
    path_count = 1;  // Ahora tenemos 1 directorio en el PATH
}

// Actualizar PATH con nuevos directorios (usado por el builtin 'path')
// new_dirs: Array de strings con los nuevos directorios
// count: Número de directorios en new_dirs (puede ser 0 para vaciar el PATH)
static void update_path(char **new_dirs, size_t count) {
    // Liberar el PATH antiguo completamente
    for (size_t i = 0; i < path_count; i++) {
        free(path_dirs[i]);
    }
    free(path_dirs);
    
    // Si count == 0, el usuario escribió "path" sin argumentos → vaciar PATH
    if (count == 0) {
        path_dirs = NULL;
        path_count = 0;
        return;
    }

    // Alocar espacio para 'count' punteros a strings
    path_dirs = malloc(count * sizeof(char*));
    if (!path_dirs) {
        util_print_error();
        exit(1);
    }

    // Copiar cada directorio del nuevo PATH
    for (size_t i = 0; i < count; i++) {
        path_dirs[i] = strdup(new_dirs[i]);  // Duplicar string
        if (!path_dirs[i]) {
            // Si falla, liberar lo que ya se alocó
            for (size_t j = 0; j < i; j++) {
                free(path_dirs[j]);
            }
            free(path_dirs);
            util_print_error();
            exit(1);
        }
    }
    path_count = count;  // Actualizar contador
}

// Buscar un ejecutable en el PATH del shell
// cmd: Nombre del comando (ej: "ls", "grep", "/bin/ls")
// Retorna: Ruta completa al ejecutable si se encuentra, NULL si no
static char *find_executable(const char *cmd) {
    // Si el comando contiene '/', es una ruta (absoluta o relativa)
    // No buscar en PATH, usar directamente
    if (strchr(cmd, '/')) {
        // Verificar si existe y es ejecutable con access()
        if (access(cmd, X_OK) == 0) {
            return strdup(cmd);  // Retornar copia de la ruta
        }
        return NULL;  // No existe o no es ejecutable
    }

    // Buscar en cada directorio del PATH en orden
    for (size_t i = 0; i < path_count; i++) {
        char *full_path = NULL;
        // asprintf aloca memoria y formatea: "/bin" + "/" + "ls" = "/bin/ls"
        if (asprintf(&full_path, "%s/%s", path_dirs[i], cmd) == -1) {
            util_print_error();
            continue;  // Si falla asprintf, probar siguiente directorio
        }
        // Verificar si el archivo existe y tiene permisos de ejecución (X_OK)
        if (access(full_path, X_OK) == 0) {
            return full_path;  // Encontrado! Retornar ruta completa
        }
        free(full_path);  // No encontrado aquí, liberar y continuar
    }
    return NULL;  // No encontrado en ningún directorio del PATH
}

// Manejar comandos incorporados (builtins): exit, cd, path
// Retorna: 1 si es un builtin (aunque falle), 0 si no es builtin
static int handle_builtin(cmd_t *cmd) {
    if (!cmd || !cmd->argv || !cmd->argv[0]) return 0;  // Validación

    // Builtin: exit
    // Debe invocarse sin argumentos, termina el shell con exit(0)
    if (strcmp(cmd->argv[0], "exit") == 0) {
        if (cmd->argv[1]) {  // Si hay argumentos después de exit -> error
            util_print_error();
            return 1;  // Es un builtin, pero con error
        }
        exit(0);  // Terminar el shell limpiamente
    }

    // Builtin: cd
    // Requiere EXACTAMENTE 1 argumento (el directorio destino)
    if (strcmp(cmd->argv[0], "cd") == 0) {
        if (!cmd->argv[1] || cmd->argv[2]) {  // 0 argumentos o más de 1 -> error
            util_print_error();
            return 1;
        }
        // chdir() cambia el directorio de trabajo del proceso
        if (chdir(cmd->argv[1]) != 0) {  // Si falla (ej: no existe)
            util_print_error();
        }
        return 1;  // Es un builtin (retornamos 1 aunque falle chdir)
    }

    // Builtin: path
    // Acepta 0 o más argumentos; reemplaza el PATH completo
    if (strcmp(cmd->argv[0], "path") == 0) {
        // Contar cuántos directorios se pasaron (argv[1], argv[2], ...)
        int count = 0;
        while (cmd->argv[count + 1]) count++;
        // Actualizar PATH global; si count==0 se vacía el PATH
        update_path(cmd->argv + 1, count);  // +1 para saltar "path"
        return 1;
    }

    return 0;  // No es un builtin, debe ejecutarse como programa externo
}

// Parsear una línea de comando en una estructura cmd_t
// line: String con el comando (puede contener redirección >)
// Retorna: cmd_t con argv y redir_file llenados, o NULL si hay error
static cmd_t *parse_command(char *line) {
    // Alocar estructura con calloc (inicializa todo a 0/NULL)
    cmd_t *cmd = calloc(1, sizeof(cmd_t));
    if (!cmd) {
        util_print_error();
        return NULL;
    }

    // PASO 1: Separar comando de redirección (buscar '>')
    char *cmd_part = line;  // Parte antes del '>'
    char *redir_part = NULL;  // Parte después del '>'
    char *gt_pos = strchr(line, '>');  // Buscar primer '>'
    
    if (gt_pos) {
        // Verificar que hay algo (no sólo espacios) antes del '>'
        char *tmp = line;
        int has_command = 0;
        while (tmp < gt_pos) {
            if (*tmp != ' ' && *tmp != '\t') {
                has_command = 1;
                break;
            }
            tmp++;
        }
        if (!has_command) {
            util_print_error();
            free_command(cmd);
            return NULL;
        }

        *gt_pos = '\0';  // Separar: reemplazar '>' con fin de string
        redir_part = gt_pos + 1;  // Apuntar a lo que viene después del '>'
        // Saltar espacios iniciales después del '>'
        while (*redir_part == ' ' || *redir_part == '\t') redir_part++;
        
        // Verificar que sólo hay UN archivo después del '>'
        char *next_token = strchr(redir_part, ' ');
        char *next_gt = strchr(redir_part, '>');
        if (!*redir_part || next_gt || (next_token && *(next_token+1) != '\0')) {
            util_print_error();
            free_command(cmd);
            return NULL;
        }
        // Guardar nombre del archivo de redirección
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

    // PASO 2: Contar tokens (palabras) en la parte del comando
    // Necesitamos saber cuántos argumentos hay para alocar argv
    int token_count = 0;
    char *tmp = strdup(cmd_part);  // Copiar porque strtok modifica el string
    if (!tmp) {
        util_print_error();
        free_command(cmd);
        return NULL;
    }
    char *token = strtok(tmp, " \t");  // Separar por espacios y tabs
    while (token) {
        token_count++;
        token = strtok(NULL, " \t");  // NULL indica "continuar con el string anterior"
    }
    free(tmp);  // Liberar copia temporal

    if (token_count == 0) {  // Línea vacía o sólo espacios
        free_command(cmd);
        return NULL;
    }

    // PASO 3: Alocar argv (array de punteros a string, terminado en NULL)
    cmd->argv = calloc(token_count + 1, sizeof(char*));  // +1 para NULL final
    if (!cmd->argv) {
        util_print_error();
        free_command(cmd);
        return NULL;
    }

    // PASO 4: Llenar argv con los tokens (palabras del comando)
    int i = 0;
    token = strtok(cmd_part, " \t");  // Volver a tokenizar (ahora guardaremos)
    while (token && i < token_count) {
        cmd->argv[i] = strdup(token);  // Duplicar cada token
        if (!cmd->argv[i]) {
            util_print_error();
            free_command(cmd);
            return NULL;
        }
        i++;
        token = strtok(NULL, " \t");
    }
    cmd->argv[i] = NULL;  // Terminar con NULL (requerido por execv)

    return cmd;  // Retornar comando parseado exitosamente
}

// Dividir una línea en comandos paralelos (separados por '&')
// Ejemplo: "ls & echo uno & pwd" -> ["ls", "echo uno", "pwd"]
// line: Línea completa con posibles '&'
// count: (salida) Número de comandos encontrados
// Retorna: Array de cmd_t*, o NULL si hay error o no hay comandos
static cmd_t **split_parallel_commands(char *line, int *count) {
    *count = 0;
    // Alocar espacio para hasta MAX_PARALLEL_CMDS comandos
    cmd_t **cmds = malloc(MAX_PARALLEL_CMDS * sizeof(cmd_t*));
    if (!cmds) {
        util_print_error();
        return NULL;
    }

    char *saveptr;  // Para strtok_r (versión reentrant de strtok)
    // strtok_r divide por '&', saveptr guarda el estado
    char *token = strtok_r(line, "&", &saveptr);
    
    while (token && *count < MAX_PARALLEL_CMDS) {
        // Eliminar espacios al inicio y final de cada comando
        while (*token == ' ' || *token == '\t') token++;
        char *end = token + strlen(token) - 1;
        while (end > token && (*end == ' ' || *end == '\t')) {
            *end = '\0';
            end--;
        }
        
        if (*token) {  // Ignorar tokens vacíos (ej: "ls & & pwd" tiene & vacío)
            cmd_t *cmd = parse_command(token);  // Parsear este comando individual
            if (cmd) {
                cmd->is_background = 1;  // Marcar como background (se ajusta después)
                cmds[*count] = cmd;
                (*count)++;
            }
        }
        
        token = strtok_r(NULL, "&", &saveptr);  // Siguiente comando
    }

    if (*count == 0) {  // No se encontró ningún comando válido
        free(cmds);
        return NULL;
    }

    // Ajuste: el último comando NO es background SALVO que la línea termine en '&'
    // Ejemplo: "ls & pwd" -> pwd NO es background (esperamos a que termine)
    // Ejemplo: "ls & pwd &" -> pwd SÍ es background
    if (*count > 0) {
        cmds[*count - 1]->is_background = (line[strlen(line) - 1] == '&');
    }

    return cmds;
}

// Ejecutar un comando (builtin o externo)
// cmd: Comando parseado con argv, redir_file, is_background
// Retorna: PID del proceso hijo (si es externo), 0 (si es builtin), -1 (error)
static int execute_command(cmd_t *cmd) {
    if (!cmd || !cmd->argv || !cmd->argv[0]) return -1;  // Validación

    // PASO 1: Verificar si es un builtin (exit, cd, path)
    // handle_builtin retorna 1 si es builtin, 0 si no
    if (handle_builtin(cmd)) {
        return 0;  // Builtin ejecutado (o intentó), no necesitamos fork
    }

    // PASO 2: Es un comando externo, buscar el ejecutable en PATH
    char *exec_path = find_executable(cmd->argv[0]);
    if (!exec_path) {  // No se encontró en PATH
        util_print_error();
        return -1;
    }

    // PASO 3: Crear proceso hijo con fork()
    pid_t pid = fork();
    if (pid == -1) {  // Error en fork
        free(exec_path);
        util_print_error();
        return -1;
    }

    if (pid == 0) {  // Código del PROCESO HIJO
        // PASO 4a: Configurar redirección de salida si se especificó '>'
        if (cmd->redir_file) {
            // Abrir archivo: crear si no existe, truncar si existe, solo escritura
            int fd = open(cmd->redir_file, O_WRONLY | O_CREAT | O_TRUNC, 0644);
            if (fd == -1) {  // Error al abrir archivo
                util_print_error();
                exit(1);
            }
            // dup2: redirigir stdout (1) y stderr (2) al archivo
            // Después de esto, printf y fprintf(stderr) escriben al archivo
            if (dup2(fd, STDOUT_FILENO) == -1 || dup2(fd, STDERR_FILENO) == -1) {
                util_print_error();
                close(fd);
                exit(1);
            }
            close(fd);  // Ya no necesitamos el descriptor original
        }

        // PASO 4b: Reemplazar el proceso hijo con el ejecutable usando execv()
        // execv NO retorna si tiene éxito (el proceso se reemplaza completamente)
        // Sólo retorna si hay error (ej: el archivo no es ejecutable)
        execv(exec_path, cmd->argv);
        util_print_error();  // Si llegamos aquí, execv falló
        exit(1);
    }

    // Código del PROCESO PADRE (el shell)
    free(exec_path);  // Ya no necesitamos la ruta
    
    // Decidir si esperamos al hijo o no
    if (!cmd->is_background) {
        // Comando normal (sin &): esperar a que termine antes de continuar
        int status;
        waitpid(pid, &status, 0);  // Bloquea hasta que el hijo termine
    }
    // Si is_background==1, NO esperamos aquí; seguimos inmediatamente
    // La espera se hace después con wait_for_children()
    
    return pid;  // Retornar PID para tracking (aunque no se usa mucho)
}

// Esperar a que terminen TODOS los procesos hijos en background
// Usado después de lanzar comandos paralelos con &
// waitpid(-1, ...) espera a CUALQUIER hijo; retorna -1 cuando no quedan hijos
static void wait_for_children(void) {
    while (waitpid(-1, NULL, 0) > 0);  // Bucle hasta que no haya más hijos
}

int main(int argc, char *argv[]) {
    // Verificar argumentos: debe ser "./gtesh" o "./gtesh archivo.txt"
    // Más de 1 argumento -> error y exit(1)
    if (argc > 2) {
        util_print_error();
        exit(1);
    }

    // Inicializar PATH con /bin 
    init_path();

    // ====== MODO BATCH (si se pasó un archivo) ======
    if (argc == 2) {
        FILE *batch = fopen(argv[1], "r");  // Abrir archivo batch
        if (!batch) {  // Archivo no existe o no se puede abrir
            util_print_error();
            exit(1);  // Salir con código 1 (según enunciado)
        }

        char *line = NULL;  // Buffer para getline (se aloca automáticamente)
        size_t len = 0;     // Tamaño del buffer
        ssize_t read;       // Número de bytes leídos

        // Leer línea por línea del archivo
        while ((read = getline(&line, &len, batch)) != -1) {
            // Eliminar newline al final si existe
            if (read > 0 && line[read-1] == '\n') {
                line[read-1] = '\0';
            }

            // Parsear la línea en comandos (puede haber varios con &)
            int cmd_count;
            cmd_t **cmds = split_parallel_commands(line, &cmd_count);
            if (cmds) {
                // Ejecutar cada comando (se lanzan en paralelo si hay &)
                for (int i = 0; i < cmd_count; i++) {
                    if (execute_command(cmds[i]) == -1) {
                        // Error ejecutando comando: continuar con el siguiente
                        continue;
                    }
                }
                // Esperar a que terminen TODOS los comandos lanzados
                wait_for_children();
                
                // Liberar memoria de todos los comandos
                for (int i = 0; i < cmd_count; i++) {
                    free_command(cmds[i]);
                }
                free(cmds);
            }
        }

        free(line);   // Liberar buffer de getline
        fclose(batch);
        exit(0);  // Terminar con código 0 (batch exitoso)
    }

    // ====== MODO INTERACTIVO (con readline para edición de línea) ======
    // Imprimir banner de bienvenida
    printf("\n\033[1;35m╔═══════════════════════════════════╗\033[0m\n");
    printf("\033[1;35m║\033[1;36m    ✦ GTESH Shell v1.0 ✦           \033[1;35m║\033[0m\n");
    printf("\033[1;35m║\033[0m  Usa Ctrl+D para salir            \033[1;35m║\033[0m\n");
    printf("\033[1;35m╚═══════════════════════════════════╝\033[0m\n\n");
    
    char *line = NULL;

    while (1) {  // Bucle infinito hasta EOF o exit
        // readline() lee con edición interactiva (flechas, historial, etc.)
        // El prompt se pasa como argumento, no necesitamos printf
        if (line) {
            free(line);  // Liberar línea anterior (readline aloca memoria)
            line = NULL;
        }
        
        line = readline(PROMPT);  // Lee línea con edición completa
        
        if (!line) {
            break;  // EOF (Ctrl+D) -> salir del bucle
        }
        
        // Si la línea no está vacía, agregarla al historial
        // Esto permite usar ↑/↓ para navegar comandos anteriores
        if (*line) {
            add_history(line);
        }

        // Parsear y ejecutar (igual que en modo batch)
        int cmd_count;
        cmd_t **cmds = split_parallel_commands(line, &cmd_count);
        if (cmds) {
            for (int i = 0; i < cmd_count; i++) {
                if (execute_command(cmds[i]) == -1) {
                    // Error: continuar (mostrar prompt de nuevo)
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

    free(line);  // Liberar buffer de getline
    
    exit(0);  // Salir limpiamente (llegamos aquí con EOF)
}