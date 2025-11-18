# Proyecto SO 

Contenido creado:
- `src/` — código fuente (`project.c`) con toda la lógica consolidada en un solo archivo.

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

## Autores
Angeles y los otros (felizzz, 中國人, justin y rafael david hernandez gonzales)

# Manual
## Shell
----------------------------------------------------------------------------------------------------------
Inicial:
gcc -Wall -Wextra src/project.c -o gtesh    #crea ejecutable
./gtesh                                     #entra al shell

Comandos básicos:
ls -la            # Muestra el listado del directorio
pwd               # Muestra la ruta actual
echo Hola Mundo   # Imprime "Hola Mundo"

Comandos incorporados (builtins):
path            # vaciar el PATH
path /bin /usr/bin   # agregar estos directorios al PATH
cd /tmp         # cambiar al directorio /tmp
pwd             # Muestra directorio /tmp
cd ..           # volver al directorio anterior

Redirección de salida: (ir antes al directorio del proyecto)

Ejemplo 1:
ls -la > listado.txt    # No muestra nada en pantalla
cat listado.txt         # Muestra el contenido del archivo

Ejemplo 2:
echo prueba_redireccion > salida.txt  # No muestra nada en pantalla
cat salida.txt                # Muestra el contenido del archivo

Comandos paralelos:
sleep 2 & echo "Primero" & echo "Segundo"   # Muestra "Primero" y "Segundo" casi inmediatamente

Manejo de errores:
cd              # Muestra error (requiere un argumento)
cd /noexiste    # Muestra error
ls > > salida.txt   # Muestra error (redirección inválida)
comandoinvalido # Muestra error
ls > salida.txt listado.txt    # varias cosas a la derecha
> salida.txt   # redirección sin comando 
ls > salida.txt > listado.txt  # múltiples '>'


Para salir:
exit            # Termina el shell

## Modo batch
----------------------------------------------------------------------------------------------------------
Crear un archivo batch (fuera de gtesh):

cat > prueba_batch.txt <<'EOF'
ls -la
echo DesdeBatch > salida_batch.txt
sleep 1 & echo uno & echo dos
cd /tmp
pwd
exit
EOF

Luego:
./gtesh prueba_batch.txt