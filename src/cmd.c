// SPDX-License-Identifier: BSD-3-Clause

#include <sys/types.h>
#include <sys/stat.h>
#include <sys/wait.h>

#include <fcntl.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include "cmd.h"
#include "utils.h"

#define READ		0
#define WRITE		1

/**
 * Internal change-directory command.
 */
static bool shell_cd(word_t *dir)
{
	// Current directory, which will become the old one after cd
	char buffer[MAX_PATH];
	char *oldpwd = getcwd(buffer, MAX_PATH);

	if (oldpwd == NULL) {
		DIE(FAILURE_CODE, "Failed to get current directory");
		return false;
	}

	int ret = setenv("OLDPWD", oldpwd, 1);

	if (ret == -1) {
		DIE(FAILURE_CODE, "Failed to set OLDPWD");
		return false;  // Return false if setting OLDPWD fails
	}

	if (dir == NULL || dir->string[0] == '\0' || strcmp(dir->string, "~") == 0) {
		return chdir(getenv("HOME"));
	} else if (strcmp(dir->string, "..") == 0) {
		return chdir("..");
	} else if (strcmp(dir->string, ".") == 0) {
		return true;
	} else if (strcmp(dir->string, "-") == 0) {
		if (getenv("OLDPWD") == NULL) {
			DIE(FAILURE_CODE, "OLDPWD not set");
			return false;
		}
		return chdir(getenv("OLDPWD"));
	} else if (access(dir->string, F_OK) == 0) {
		return chdir(dir->string);
	}

	return true;
}

/**
 * Internal exit/quit command.
 */
static int shell_exit(void)
{
	exit(SUCCESS_CODE);
	return SUCCESS_CODE;
}

/**
 * Get the value of a token (environment variable or string).
 * If the token has expansion enabled, it will expand it into the environment variable value.
 * Otherwise, it will return the token's string.
 */
static const char *expand_token(word_t *word)
{
	if (word->expand) {
		// If the word should be expanded (like a variable), get the environment value
		const char *env_value = getenv(word->string);

		if (env_value == NULL)
			return "";
		return env_value;
	}
	return word->string;
}

/**
 * Concatenates the string values of a linked list of tokens into a single string.
 */
static char *token_to_string(word_t *token)
{
	if (!token)
		return NULL;

	// Start with the value of the first token
	const char *first_value = expand_token(token);
	size_t value_length = strlen(first_value) + 1;  // +1 for null terminator

	// Count the length of all token parts
	word_t *current_token = token->next_part;

	while (current_token) {
		value_length += strlen(expand_token(current_token));
		current_token = current_token->next_part;
	}

	// Allocate memory for the final concatenated string
	char *value = malloc(value_length);

	if (!value) {
		DIE(FAILURE_CODE, "Memory allocation failed");
		return NULL;
	}

	// Copy the first token's value into the allocated string and append the remaining parts
	memcpy(value, first_value, strlen(first_value) + 1);

	current_token = token->next_part;

	while (current_token) {
		strcat(value, expand_token(current_token));
		current_token = current_token->next_part;
	}

	return value;
}

/*
 * Checks if there are any redirections to be done and performs them.
 * Returns true if redirection was successful, false otherwise.
 */
static bool manage_redirections(simple_command_t *s)
{
	if (!s)
		return false;

	// Get the file names for input, output, and error redirection
	char *in_val = s->in ? token_to_string(s->in) : NULL;
	char *out_val = s->out ? token_to_string(s->out) : NULL;
	char *err_val = s->err ? token_to_string(s->err) : NULL;

	bool success = true;

	// Handle Input Redirection (<)
	if (s->in && in_val) {
		int in_fd = open(in_val, O_RDONLY);

		if (in_fd < 0) {
			DIE(FAILURE_CODE, "Error opening input file for redirection");
			success = false;
		} else if (dup2(in_fd, STDIN_FILENO) < 0) {
			DIE(FAILURE_CODE, "Error redirecting standard input");
			success = false;
		}
		close(in_fd);
	}

	// Handle Output Redirection (> or >>)
	if (s->out && out_val) {
		int out_fd;

		if (s->io_flags) {  // If append mode (>>)
			out_fd = open(out_val, O_WRONLY | O_CREAT | O_APPEND, 0644);
		} else {  // Overwrite mode (>)
			out_fd = open(out_val, O_WRONLY | O_CREAT | O_TRUNC, 0644);
		}

		if (out_fd < 0) {
			DIE(FAILURE_CODE, "Error opening output file for redirection");
			success = false;
		} else if (dup2(out_fd, STDOUT_FILENO) < 0) {
			DIE(FAILURE_CODE, "Error redirecting standard output");
			success = false;
		}
		close(out_fd);
	}

	// Handle Error Redirection (2> or 2>>)
	if (s->err && err_val) {
		int err_fd;

		if (s->io_flags) {  // If append mode (2>>)
			err_fd = open(err_val, O_WRONLY | O_CREAT | O_APPEND, 0644);
		} else {  // Overwrite mode (2>)
			err_fd = open(err_val, O_WRONLY | O_CREAT | O_TRUNC, 0644);
		}

		if (err_fd < 0) {
			DIE(FAILURE_CODE, "Error opening error file for redirection");
			success = false;
		} else if (dup2(err_fd, STDERR_FILENO) < 0) {
			DIE(FAILURE_CODE, "Error redirecting standard error");
			success = false;
		}
		close(err_fd);
	}

	// Combined output and error redirection (&>)
	if (s->out && s->err && strcmp(out_val, err_val) == 0) {
		int combined_fd = open(out_val, O_WRONLY | O_CREAT | O_TRUNC, 0644);

		if (combined_fd < 0) {
			DIE(FAILURE_CODE, "Error opening combined output and error file for redirection");
			success = false;
		} else if (dup2(combined_fd, STDOUT_FILENO) < 0 || dup2(combined_fd, STDERR_FILENO) < 0) {
			DIE(FAILURE_CODE, "Error redirecting standard output and error");
			success = false;
		}
		close(combined_fd);
	}

	free(in_val);
	free(out_val);
	free(err_val);

	return success;
}

/**
 * Perform the cd command.	
 */
static int execute_cd(simple_command_t *s)
{
	int original = dup(STDOUT_FILENO);

	if (original < 0) {
		DIE(FAILURE_CODE, "Failed to backup STDOUT");
		return FAILURE_CODE;
	}

	if (!manage_redirections(s)) {
		close(original);
		return FAILURE_CODE;
	}

	int cd_result = shell_cd(s->params);

	fflush(stdout);

	if (dup2(original, STDOUT_FILENO) < 0) {
		DIE(FAILURE_CODE, "Failed to restore STDOUT");
		close(original);
		return FAILURE_CODE;
	}

	close(original);
	return cd_result;
}

/**
 * Perform an external command.
 */
static int execute_external_command(simple_command_t *s)
{
	pid_t pid = fork();

	char *command_path = get_word(s->verb);
	int argc;
	char **argv = get_argv(s, &argc);

	if (pid == -1) {
		DIE(FAILURE_CODE, "fork");
	} else if (pid == 0) {
		if (!manage_redirections(s))
			exit(FAILURE_CODE);

		execvp(command_path, argv);

		// if execvp fails
		exit(FAILURE_CODE);
	} else {
		int status;

		waitpid(pid, &status, 0);

		if (WIFEXITED(status))
			return WEXITSTATUS(status);
	}

	return SUCCESS_CODE;
}

/**
 * Perform an environment variable assignment.
 */
static int execute_env_var_assignment(simple_command_t *s)
{
	const char *var = s->verb->string;
	char *new_value = token_to_string(s->verb->next_part->next_part);
	int ret = setenv(var, new_value, 1);

	if (ret == -1) {
		DIE(FAILURE_CODE, "setenv");
		free(new_value);
		return FAILURE_CODE;
	}

	free(new_value);
	return SUCCESS_CODE;
}

/**
 * Parse a simple command (internal, environment variable assignment,
 * external command).
 */
static int parse_simple(simple_command_t *s, int level, command_t *father)
{
	if (!s || !s->verb || !s->verb->string)
		return FAILURE_CODE;

	/* If builtin command, execute the command. */
	if (strcmp(s->verb->string, "cd") == 0)
		return execute_cd(s);

	if (strcmp(s->verb->string, "exit") == 0 || strcmp(s->verb->string, "quit") == 0)
		return shell_exit();


	/* If variable assignment, execute the assignment */
	if (s->verb && s->verb->next_part && s->verb->next_part->string && s->verb->next_part->string[0] == '=')
		return execute_env_var_assignment(s);

	/* If it's not any of the above, it's an external command*/
	return execute_external_command(s);
}

/**
 * Process two commands in parallel, by creating two children.
 */
static bool run_in_parallel(command_t *cmd1, command_t *cmd2, int level,
		command_t *father)
{
	/* Execute cmd1 and cmd2 simultaneously. */

	return true;
}


/**
 * Run commands by creating an anonymous pipe (cmd1 | cmd2).
 */
static bool run_on_pipe(command_t *cmd1, command_t *cmd2, int level,
		command_t *father)
{
	/* Redirect the output of cmd1 to the input of cmd2. */

	return true; 
}

/**
 * Parse and execute a command.
 */
int parse_command(command_t *c, int level, command_t *father)
{
	if (!c)
		return FAILURE_CODE;

	/* Execute a simple command. */
	if (c->op == OP_NONE)
		return parse_simple(c->scmd, level, father);

	switch (c->op) {
	case OP_SEQUENTIAL:
		parse_command(c->cmd1, level + 1, c);
		return parse_command(c->cmd2, level + 1, c);

	case OP_PARALLEL:
		return run_in_parallel(c->cmd1, c->cmd2, level, father);

	case OP_CONDITIONAL_NZERO:
		if (parse_command(c->cmd1, level, c) == 0)
			return SUCCESS_CODE;
		return parse_command(c->cmd2, level, c);

	case OP_CONDITIONAL_ZERO:
		if (parse_command(c->cmd1, level, c) != 0)
			return SUCCESS_CODE;
		return parse_command(c->cmd2, level, c);

	case OP_PIPE:
		return run_on_pipe(c->cmd1, c->cmd2, level, father);

	default:
		return SHELL_EXIT;
	}

	return SUCCESS_CODE;

}
