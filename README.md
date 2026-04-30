![Language](https://img.shields.io/badge/language-C-blue)
![Threads](https://img.shields.io/badge/library-pthreads-green)
![Platform](https://img.shields.io/badge/platform-Linux-lightgrey)
## Table of Contents

- [Project Overview](#project-overview)
- [Features](#features)
- [Project Structure](#project-structure)
- [How to Run](#how-to-run)
- [Example Output](#example-output)
- [Technologies Used](#technologies-used)
- [Learning Outcomes](#learning-outcomes)
- [Author](#author)

  
Multithreaded Process Manager Simulator
======================================

A simulation of an operating system process manager implemented in C using POSIX threads.  
The project demonstrates how processes are created, managed, and terminated in a concurrent system.

---

Project Overview
----------------

This simulator models a simplified operating system process manager.  
Multiple worker threads execute commands from script files while a monitor thread observes system state changes.

The system maintains a shared process table that stores information about all active processes.

---

Key Features
------------

• Simulated process table supporting up to 64 processes  
• Process Control Blocks (PCB)  
• Parent–child process relationships  
• Process states (RUNNING, BLOCKED, ZOMBIE)  
• Multithreaded execution using POSIX threads  
• Mutex-based synchronization  
• Monitor thread that logs system snapshots  

---

System Architecture
-------------------
         +----------------------+
         |   Script Files       |
         | thread0.txt ...      |
         +----------+-----------+
                    |
                    v
            +--------------+
            | Worker       |
            | Threads      |
            +--------------+
                    |
                    v
           +------------------+
           | Process Manager  |
           | (Shared Table)   |
           +------------------+
                    |
                    v
           +------------------+
           | Monitor Thread   |
           | prints snapshots |
           +------------------+
                    |
                    v
             snapshots.txt
             
---

Supported Commands
------------------

| Command | Description |
|-------|-------------|
| fork parent_pid | Create a child process |
| exit pid status | Terminate a process |
| wait parent child | Parent waits for child |
| kill pid | Terminate a process |
| sleep ms | Pause thread execution |

---

Compilation
-----------
```bash
gcc src/pm_sim.c -o pm_sim -lpthread
```

---

Run the Program
---------------
```bash
./pm_sim scripts/thread0.txt scripts/thread1.txt scripts/thread2.txt scripts/thread3.txt
```

Each script file is executed by a separate worker thread.

---

Example Output
--------------

![Process Snapshot](output_example.png)

Snapshots of the process table are stored in:

`output/snapshots.txt`

---

Technologies Used
-----------------

• C Programming  
• POSIX Threads (pthreads)  
• GCC Compiler  
• Linux / Unix Environment  

---

Learning Outcomes
-----------------

This project demonstrates practical knowledge of:

• Operating system process management  
• Thread synchronization  
• Concurrent programming  
• Systems-level programming in C  

---

Author
------

Taspiha Tabassum  
Computer Science Student  
BRAC University
