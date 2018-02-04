

#include <common_defs.h>
#include <string_util.h>
#include <hal.h>

#include <irq.h>
#include <kmalloc.h>
#include <paging.h>
#include <debug_utils.h>
#include <process.h>
#include <elf_loader.h>

#include <utils.h>
#include <tasklet.h>
#include <sync.h>

#include <fs/fat32.h>
#include <fs/exvfs.h>

#include <kb.h>
#include <timer.h>
#include <term.h>

#include <self_tests/self_tests.h>

task_info *usermode_init_task = NULL;

void show_hello_message()
{
   printk("Hello from exOS!\n");
   printk("TIMER_HZ: %i\n", TIMER_HZ);
}

void load_usermode_init()
{
   void *entry_point = NULL;
   void *stack_addr = NULL;
   page_directory_t *pdir = NULL;

   load_elf_program("/sbin/init", &pdir, &entry_point, &stack_addr);

   usermode_init_task =
      create_first_usermode_task(pdir, entry_point, stack_addr);

   printk("[load_usermode_init] Entry: %p\n", entry_point);
   printk("[load_usermode_init] Stack: %p\n", stack_addr);
}


void mount_ramdisk(void)
{
   printk("Mapping the ramdisk at %p (%d pages)... ",
          RAMDISK_VADDR, RAMDISK_SIZE / PAGE_SIZE);

   map_pages(get_kernel_page_dir(),
             (void *) RAMDISK_VADDR,
             RAMDISK_PADDR,
             RAMDISK_SIZE / PAGE_SIZE,
             false,
             true);

   printk("DONE\n");

   filesystem *root_fs = fat_mount_ramdisk((void *)RAMDISK_VADDR);
   mountpoint_add(root_fs, "/");
}

// CRC32 failure at 26M + 8K.
void ramdisk_checksum(void)
{
   printk("Calculating the RAMDISK's CRC32...\n");

   for (int k=0; k <= 16; k++) {
      u32 result = crc32(0, (void*)RAMDISK_VADDR, 26*MB + k*KB);
      printk("CRC32 for M=26, K=%u: %p\n", k, result);
   }

   // u32 result = crc32(0, (void*)RAMDISK_VADDR, RAMDISK_SIZE);

   // for (int off = 0x1a*MB; off < 35*MB; off+=MB) {

   //    printk("[%p]: ", off);
   //    for (int i = 0; i < 8; i++) {
   //       printk("0x%x ", (int)((u8*)RAMDISK_VADDR+off)[i]);
   //    }
   //    printk("\n");

   // }
}

void kmain()
{
   term_init();
   show_hello_message();

   setup_segmentation();
   setup_interrupt_handling();

   init_pageframe_allocator();
   initialize_kmalloc();

   init_paging();

   initialize_scheduler();
   initialize_tasklets();

   set_timer_freq(TIMER_HZ);

   irq_install_handler(X86_PC_TIMER_IRQ, timer_handler);
   irq_install_handler(X86_PC_KEYBOARD_IRQ, keyboard_handler);

   // TODO: make the kernel actually support the sysenter interface
   setup_sysenter_interface();

   mount_ramdisk();
   ramdisk_checksum();
   while(1) halt();

   DEBUG_ONLY(bool tasklet_added =) add_tasklet0(&init_kb);
   ASSERT(tasklet_added);

   //kthread_create(&simple_test_kthread, (void*)0xAA1234BB);
   //kmutex_test();
   //kcond_test();

   //kernel_kmalloc_perf_test();

   //kthread_create(&sleeping_kthread, (void *) 123);
   //kthread_create(&sleeping_kthread, (void *) 20);
   //kthread_create(&sleeping_kthread, (void *) (10*TIMER_HZ));
   //kthread_create(&tasklet_stress_test, NULL);

   load_usermode_init();

   printk("[kernel main] Starting the scheduler...\n");
   switch_to_idle_task_outside_interrupt_context();

   // We should never get here!
   NOT_REACHED();
}
