// implement fork from user space

#include <inc/string.h>
#include <inc/lib.h>

// PTE_COW marks copy-on-write page table entries.
// It is one of the bits explicitly allocated to user processes (PTE_AVAIL).
#define PTE_COW		0x800

//
// Custom page fault handler - if faulting page is copy-on-write,
// map in our own private writable copy.
//
static void
pgfault(struct UTrapframe *utf)
{
	void *addr = (void *) utf->utf_fault_va;
	uint32_t err = utf->utf_err;
	int r;

	// Check that the faulting access was (1) a write, and (2) to a
	// copy-on-write page.  If not, panic.
	// Hint:
	//   Use the read-only page table mappings at uvpt
	//   (see <inc/memlayout.h>).
	
	// LAB 4: Your code here.
	if ((err & FEC_WR) != FEC_WR || (uvpt[PGNUM(addr)] & PTE_COW) != PTE_COW)
		panic("access type is not writable or not copy-on-write.");
	// Allocate a new page, map it at a temporary location (PFTEMP),
	// copy the data from the old page to the new page, then move the new
	// page to the old page's address.
	// Hint:
	//   You should make three system calls.

	// LAB 4: Your code here.
	addr = ROUNDDOWN(addr, PGSIZE);
	r = sys_page_alloc(0, (void *) PFTEMP, PTE_U | PTE_W);
	if (r < 0)
		panic("error %e happen in pagefault.", r);
	
	memcpy((void *) PFTEMP, addr, PGSIZE);
	r = sys_page_map(0, (void *) PFTEMP, 0, addr, PTE_U | PTE_W);
	if (r < 0)
		panic("error %e happen in pagefault.", r);
	
	r = sys_page_unmap(0, (void *) PFTEMP);
	if (r < 0)
		panic("error %e happen in pagefault.", r);
	
	//panic("pgfault not implemented");
}

//
// Map our virtual page pn (address pn*PGSIZE) into the target envid
// at the same virtual address.  If the page is writable or copy-on-write,
// the new mapping must be created copy-on-write, and then our mapping must be
// marked copy-on-write as well.  (Exercise: Why do we need to mark ours
// copy-on-write again if it was already copy-on-write at the beginning of
// this function?)
//
// Returns: 0 on success, < 0 on error.
// It is also OK to panic on error.
//
/** questions answer **/
/** The ordering here (i.e., marking a page as COW 
 * in the child before marking it in the parent) actually matters. Why?
 * 关键在于栈所在的页。
 * 若先映射父进程的栈页面为PTE_COW，
 * 那么在执行第二个sys_page_map时，
 * 由于函数调用必定会对进程的栈产生写操作；
 * 而父进程的栈此时已经被标记为PTE_COW，所以会导致页错误；
 * 进程的页错误处理程序会重新申请一个物理页，并让原先的栈所在的虚拟页指向这个物理页，
 * 且父进程对这个物理页是有写权限的；
 * 随后，问题出现了：当回到引起页错误的第二个sys_page_map继续执行时，
 * 为子进程建立的映射页会使它的栈指向该刚刚申请的物理页且权限为PTE_COW，
 * 与此同时父进程却可以写入该页面，这就与写时复制的规则相违背了！
 * 第二个问题是：为什么即使父进程的页面已经为PTE_COW的情况下也还要再对其做一次映射？
 * 原理和前面类似，依然是考虑栈页面。若父进程的栈页面是PTE_COW的，
 * 在对子进程建立映射时会发生和前面相同的情况，之后必须把已经被改为父进程可写的新的栈页面再次映射为PTE_COW。**/
static int
duppage(envid_t envid, unsigned pn)
{
	int r;

	// LAB 4: Your code here.
	//panic("duppage not implemented");
	void *addr = (uintptr_t *) (pn * PGSIZE);
	if ((uvpt[pn] & PTE_W) || (uvpt[pn] & PTE_COW)){
		r = sys_page_map(0, addr, envid, addr, PTE_U | PTE_COW);
		if (r < 0)
			panic("err %e happens in mapping parent's pages which are PTE_W or PTE_COW to child's.", r);
		
		r = sys_page_map(0, addr, 0, addr, PTE_U | PTE_COW);
		if (r < 0)
			panic("err %e happens in remapping parent's pages in PTE_COW.", r);
	}else if (uvpt[pn] & PTE_SHARE){
		if ((r = sys_page_map(0, addr, envid, addr, PTE_SYSCALL)) < 0)
			panic("err %e happens in remapping parent's pages in PTE_SHARE.", r);
	}else{
		if ((r = sys_page_map(0, addr, envid, addr, PTE_U)) < 0)
			panic("err %e happen in mapping parent's pages which are only PTE_U to child's.", r);
	}
	return 0;
}

//
// User-level fork with copy-on-write.
// Set up our page fault handler appropriately.
// Create a child.
// Copy our address space and page fault handler setup to the child.
// Then mark the child as runnable and return.
//
// Returns: child's envid to the parent, 0 to the child, < 0 on error.
// It is also OK to panic on error.
//
// Hint:
//   Use uvpd, uvpt, and duppage.
//   Remember to fix "thisenv" in the child process.
//   Neither user exception stack should ever be marked copy-on-write,
//   so you must allocate a new page for the child's user exception stack.
//
envid_t
fork(void)
{
	// LAB 4: Your code here.
	//panic("fork not implemented");
	extern void _pgfault_upcall(void);
	set_pgfault_handler(pgfault);
	envid_t envid = sys_exofork();
	if (envid < 0){
		panic("creating child env failed in fork.");
	}else if (envid == 0){
		thisenv = &envs[ENVX(sys_getenvid())];
		return 0;
	}else{
		/** copy the address place under USTACKTOP **/
		uintptr_t addr = 0;
		for (; addr < USTACKTOP; addr += PGSIZE){
			if ((uvpd[PDX(addr)] & PTE_P) && (uvpt[PGNUM(addr)] & PTE_P) && 
			     (uvpt[PGNUM(addr)] & PTE_U))
				duppage(envid, addr / PGSIZE);
		}
		/** create UXSTACKTOP and set pagefault func for child env  **/
		int r = sys_page_alloc(envid, (void *) (UXSTACKTOP - PGSIZE), PTE_U | PTE_W);
		if (r < 0)
			panic("error %e happens in allocing uxstack for child env.", r);
		if ((r = sys_env_set_pgfault_upcall(envid, _pgfault_upcall)) < 0){
			panic("error %e happens in setting pagefault func for child env.", r);
		}
		/** set child env's status to be runable **/
		r = sys_env_set_status(envid, ENV_RUNNABLE);
		if (r < 0)
			panic("err % e happens in setting child env status.", r);
		return envid;
	}
		
}

// Challenge!
int
sfork(void)
{
	panic("sfork not implemented");
	return -E_INVAL;
}
