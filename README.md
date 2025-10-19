# Mini Shell

A simplified Unix-like shell implemented in **C**.  
It supports executing built-in and external commands, handling environment variables, and managing input/output redirection, pipes, and sequential execution.

---

## Overview
This project replicates the core functionality of a command-line interpreter.  
It parses commands, executes them via system calls (`fork`, `execvp`, `dup2`, `waitpid`), and manages redirections and environment variables dynamically.

**Language:** C  
**Focus:** System calls, process management, I/O redirection, shell parsing  

---

## Features

### Built-in Commands
- **`cd`** — change the current working directory (`cd`, `cd ..`, `cd -`, `cd ~`)  
- **`exit` / `quit`** — close the shell and free resources  

### Environment Variables
- Supports assignments (`VAR=value`) and variable expansion (`$VAR`)  
- Allows dynamic updates using `setenv` and `getenv`  

### Command Execution
- Runs external programs via `execvp`  
- Supports conditional execution (`&&`, `||`), sequencing (`;`), and piping (`|`)  
- Implements input (`<`), output (`>`), append (`>>`), and error redirection (`2>`, `2>>`)  

### Architecture
- **`cmd.c`** — core command execution (built-ins, redirections, pipes, conditions)  
- **`utils.c`** — string parsing and argument handling for `execvp`  
- **`main.c`** — user input loop, command parsing, and interactive shell interface  

---

## Example Session
```
VAR=hello
echo $VAR
hello
cd ..
ls -l | grep ".c"
exit
```

---

## Learning Outcomes
- Implemented process creation, synchronization, and inter-process communication.  
- Practiced redirection, piping, and file descriptor manipulation.  
- Deepened understanding of command parsing and execution in Unix systems.