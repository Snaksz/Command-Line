# Command-Line
Command Line
Project Recap:

This assignment focused on building a small Unix shell program named tsh. The goal was to gain a deeper understanding of process control, signals, and pipes by implementing key features of a shell.

Key Features:

Command Line Interpretation: The shell accepted commands and arguments from the user.
Built-in Commands: Supported built-in commands like quit, jobs, bg, and fg for managing jobs and exiting the shell.
Job Management: Managed background and foreground jobs, allowing users to switch between them and view their status.
Input/Output Redirection: Enabled users to redirect input and output to/from files.
Signal Handling: Handled signals like SIGINT (ctrl-c) and SIGTSTP (ctrl-z) to manage foreground jobs and process termination.
Pipes: Supported chaining commands together using pipes ('|').
Challenges and Solutions:

Signal Handling: Ensuring correct signal handling and process group management was crucial.
Job Management: Tracking and managing job states, especially when dealing with background jobs, required careful attention.
Pipes: Implementing pipes involved understanding how to connect processes and handle communication between them.

