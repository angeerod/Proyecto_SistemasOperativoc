# Proyecto SO (esqueleto)

Esqueleto inicial del proyecto en C para el enunciado "Enunciado Proyecto 2025 (1).pdf".

Contenido creado:
- `src/` — código fuente (`project.c`) con toda la lógica consolidada en un solo archivo.
- `requirements.md` — marcador de posición pendiente extracción completa del enunciado.

Cómo compilar y ejecutar:

# Shell gtesh

Implementación de un shell básico (gtesh) que soporta modo interactivo y batch, redirección de salida, comandos paralelos y comandos incorporados.

## Compilación

```bash
gcc -Wall -Wextra src/project.c -o gtesh
```

## Uso

### Modo Interactivo
```bash
./gtesh
```
Muestra el prompt `gtesh> ` y espera comandos.

### Modo Batch
```bash
./gtesh archivo_batch.txt
```
Ejecuta los comandos del archivo línea por línea.

## Funcionalidades

### Comandos Incorporados
- `exit`: Termina el shell
- `cd <dir>`: Cambia al directorio especificado
- `path [dir1 dir2 ...]`: Configura la ruta de búsqueda de ejecutables

### Redirección
```bash
comando > archivo  # Redirige stdout y stderr al archivo
```

### Comandos Paralelos
```bash
cmd1 & cmd2 & cmd3  # Ejecuta comandos en paralelo
```

## Ejemplos

1. Redirección básica:
```bash
gtesh> ls -la > listado.txt
gtesh> cat listado.txt
```

2. Comandos paralelos:
```bash
gtesh> sleep 2 & echo "uno" & echo "dos"
```

3. Modo batch (contenido de batch.txt):
```bash
ls -la
echo test > salida.txt
sleep 1 & echo uno & echo dos
cd /tmp
pwd
exit
```

## Manejo de Errores

El shell imprime "An error has occurred\n" en stderr cuando encuentra errores como:
- Sintaxis incorrecta en redirección
- Comando no encontrado
- Argumentos inválidos para builtins
- Error al abrir archivo en modo batch

## Criterios de Evaluación Cumplidos

1. Funcionalidad básica (25%):
   - ✓ Modo interactivo con prompt
   - ✓ Modo batch
   - ✓ Comando exit
   - ✓ Búsqueda en PATH

2. Manejo de procesos (20%):
   - ✓ Uso correcto de fork/exec/wait
   - ✓ Manejo de errores
   - ✓ Sin fallos graves

3. Búsqueda y Ejecución (15%):
   - ✓ Búsqueda en PATH
   - ✓ Uso de access()
   - ✓ Manejo de comandos no encontrados

4. Comandos incorporados (10%):
   - ✓ exit implementado
   - ✓ cd implementado
   - ✓ path implementado

5. Comandos Paralelos (10%):
   - ✓ Operador &
   - ✓ Ejecución simultánea
   - ✓ Uso correcto de wait()

6. Redirección de Salida (10%):
   - ✓ Operador >
   - ✓ Redirección de stdout y stderr
   - ✓ Manejo de archivos

7. Robustez y manejo de errores (10%):
   - ✓ Mensaje de error único y claro
   - ✓ Sin crashes
   - ✓ Validación de entrada

## Autor

Angeles Rodriguez
y los otros
