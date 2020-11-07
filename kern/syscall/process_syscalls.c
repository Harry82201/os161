#include <types.h>
#include <current.h>
#include <proc.h>
#include <kern/limits.h>
#include <limits.h>
#include <syscall.h>
#include <addrspace.h>
#include <filetable.h>
#include <kern/errno.h>
#include <mips/trapframe.h>
#include <copyinout.h>

int sys_getpid(int *retval)
{
    lock_acquire(pid_table->pt_lock);

    *retval = curproc->pid;

    lock_release(pid_table->pt_lock);
    return 0;
}

static void enter_usermode(void *data1, unsigned long data2) 
{
    (void) data2;
    void *tf = (void *) curthread->t_stack + 16;

	memcpy(tf, (const void *) data1, sizeof(struct trapframe));
	kfree((struct trapframe *) data1);

    as_activate();
    mips_usermode(tf);
}

int sys_fork(struct trapframe *tf, int *retval)
{
    struct proc *new_proc;
    int err;

    new_proc = proc_create("forked process");
    if (new_proc == NULL) {
        return ENOMEM;
    }

    err = pid_table_add_proc(new_proc, &new_proc->pid);
    if (err) {
        proc_destroy(new_proc);
        return err;
    }
    // copy addrspace
    err = as_copy(curproc->p_addrspace, &new_proc->p_addrspace);
    if (err) {
        pid_table_clear_pid(new_proc->pid);
        proc_destroy(new_proc);
        return err;
    }

    spinlock_acquire(&curproc->p_lock);
    if (curproc->p_cwd != NULL) {
        VOP_INCREF(curproc->p_cwd);
        new_proc->p_cwd = curproc->p_cwd;
    }
    spinlock_release(&curproc->p_lock);

    // copy file_table 

    struct file_table* ft = curproc->p_filetable;
    lock_acquire(ft->ft_lock);
    // ref_count to entries are incremented
    for (int i = 0; i < OPEN_MAX; i++) {
        if(ft->ft_entries[i] != NULL) {
            lock_acquire(ft->ft_entries[i]->entry_lock);
            entry_incref(ft->ft_entries[i]);
            lock_release(ft->ft_entries[i]->entry_lock);
        }
    }
    new_proc->p_filetable = ft;
    lock_release(ft->ft_lock);
    

    struct trapframe* fork_tf = kmalloc(sizeof(struct trapframe));
    memcpy(fork_tf, tf, sizeof(struct trapframe));
    fork_tf->tf_v0 = 0;
    fork_tf->tf_v1 = 0;
    fork_tf->tf_a3 = 0;
    fork_tf->tf_epc += 4;
    
    err = thread_fork("forked thread", new_proc, enter_usermode, fork_tf, 1);
    if (err) {
        lock_acquire(pid_table->pt_lock);
        pid_table_clear_pid(new_proc->pid);
        lock_release(pid_table->pt_lock);
        proc_destroy(new_proc);
        kfree(fork_tf);
        return err;
    }
    *retval = new_proc->pid;

    return 0;
}

int sys_execv(const char *program, char **args)
{
    (void) program;
    (void) args;

    return 0;
}

int sys_waitpid(pid_t pid, int *status, int options)
{

    if (pid < PID_MIN || pid > PID_MAX || pid_table->pt_process[pid] == READY) {
        return ESRCH;
    }
    if (options != 0) {
        return EINVAL;
    }

    struct proc *child = pid_table->pt_process[pid];
    for (unsigned i = 0; i < array_num(curproc->p_children); i++) {
        if (array_get(curproc->p_children, i) == child) {
            break;
        }
        if (i == array_num(curproc->p_children)) {
            return ECHILD;
        }
    }

    lock_acquire(pid_table->pt_lock);
    
    while(pid_table->pt_status[pid] != ZOMBIE) {
        cv_wait(pid_table->pt_cv, pid_table->pt_lock);
    }
    int waitcode = pid_table->pt_waitcode[pid];

    lock_release(pid_table->pt_lock);

    if (status != NULL) {
        int err = copyout(&waitcode, (userptr_t) status, sizeof(int32_t));
        if (err) {
            return err;
        }
    }

    return 0;
}

void sys__exit(int waitcode)
{
    lock_acquire(pid_table->pt_lock);

    // update children proc status
    int num_child = array_num(curproc->p_children);
    // start at most recent child
    for(int i = num_child - 1; i > 0; i--) {
        struct proc *child = array_get(curproc->p_children, i);
        
        // tell children that their parent is gone :(
        if (pid_table->pt_status[child->pid] == RUNNING) {
            pid_table->pt_status[child->pid] = ORPHAN;

        // clear zombie children to free up pids in the pid table
        } else if (pid_table->pt_status[child->pid] == ZOMBIE) {
            if (child->pid < pid_table->pt_next) {
                pid_table->pt_next = child->pid;
            }
            pid_table_clear_pid(child->pid);
            proc_destroy(child);
        } else {
            panic("Tried to modify a child that did not exist. \n");
        }
    }

    // update current proc status
    if (pid_table->pt_status[curproc->pid] == RUNNING) {
        pid_table->pt_status[curproc->pid] = ZOMBIE;
        pid_table->pt_waitcode[curproc->pid] = waitcode;
    } else if (pid_table->pt_status[curproc->pid] == ORPHAN) {
        proc_destroy(curproc);
        pid_table_clear_pid(curproc->pid);
    } else {
        panic("Tried to remove a bad process. \n");
    }

    cv_broadcast(pid_table->pt_cv, pid_table->pt_lock);

    lock_release(pid_table->pt_lock);

    thread_exit();
}