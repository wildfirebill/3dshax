#include <stdlib.h>
#include <stdint.h>
#include <string.h>

#include <3ds.h>

#include "arm9_a11kernel.h"

extern u32 RUNNINGFWVER;

u32 slabheap_physaddr = 0x1FFA0000;
u32 slabheap_vaddr = 0;

u32 arm11kernel_initialize_slabheapvaddr()
{
	u32 pos;
	u8 *ptr8;

	for(pos=0; pos<0x100; pos++)
	{
		ptr8 = mmutable_convert_vaddr2physaddr((u32*)0x1FFF8000, 0xFFF00000 + (pos<<12), NULL);
		if((u32)ptr8 == slabheap_physaddr)
		{
			slabheap_vaddr = 0xFFF00000 + (pos<<12);
			return 0;
		}
	}

	return 1;
}

u32 *get_kprocessptr(u64 procname, u32 num, u32 get_mmutableptr)
{
	u32 pos=0;
	//u32 pos2=0;
	u32 kcodeset_adr=0;
	u32 *wram = (u32*)0x1FF80000;
	u32 *kprocess = NULL;
	u32 kernelfcram_phys2vaddr_value = 0xd0000000;
	u32 kprocess_adjustoffset = 0;

	if(((u8)RUNNINGFWVER)>=44)//v8.0
	{
		kernelfcram_phys2vaddr_value = 0xc0000000;
		kprocess_adjustoffset = 8;
	}

	if(RUNNINGFWVER & 0x40000000)kprocess_adjustoffset+= 8;

	if(slabheap_vaddr==0)
	{
		if(arm11kernel_initialize_slabheapvaddr())return NULL;
	}

	for(pos=(slabheap_physaddr - (u32)wram)>>2; pos<(0x80000>>2)-1; pos++)//kcodeset_adr = KCodeSet object arm11 kernel vaddr for the specified process name.
	{
		if(wram[pos]==((u32)procname) && wram[pos+1]==((u32)(procname>>32)))
		{
			kcodeset_adr = ((pos<<2) - 0x50) + 0x1FF80000;
			kcodeset_adr = (kcodeset_adr - slabheap_physaddr) + slabheap_vaddr;
			break;
		}
	}

	if(kcodeset_adr==0)return NULL;

	for(pos=0; pos<(0x80000>>2); pos++)//kprocess = physical addr for KProcess object containing the above KCodeSet addr.
	{
		if(wram[pos]==kcodeset_adr)
		{
			if(num)
			{
				num--;
			}
			else
			{
				kprocess = &wram[pos - ((0xa8+kprocess_adjustoffset)>>2)];
				break;
			}
		}
	}

	if(kprocess==NULL)return NULL;

	if(get_mmutableptr)return (u32*)(kprocess[(0x54+kprocess_adjustoffset)>>2] - kernelfcram_phys2vaddr_value);
	return kprocess;
}

u8 *get_kprocess_contextid(u32 *kprocess)
{
	u32 kprocess_adjustoffset = 0;

	if(kprocess==NULL)return NULL;

	if(RUNNINGFWVER & 0x40000000)kprocess_adjustoffset+= 8;

	return (u8*)&kprocess[(0x44 + kprocess_adjustoffset) >> 2];
}

u8 *mmutable_convert_vaddr2physaddr(u32 *mmutable, u32 vaddr, u32 *remaining_pagesize)
{
	u32 *ptr;
	u32 val;

	if(mmutable==NULL)return NULL;

	val = mmutable[vaddr >> 20];
	if((val & 0x3) == 0x0 || (val & 0x3) == 0x3)return NULL;
	if((val & 0x3) == 0x2)
	{
		if(((val >> 18) & 1) == 0)
		{
			if(remaining_pagesize)*remaining_pagesize = 0x100000 - (vaddr & 0xFFFFF);

			return (u8*)((val & ~0xFFFFF) | (vaddr & 0xFFFFF));
		}

		if(remaining_pagesize)*remaining_pagesize = 0x1000000 - (vaddr & 0xFFFFFF);

		return (u8*)((val & ~0xFFFFFF) | (vaddr & 0xFFFFFF));
	}

	ptr = (u32*)(val & ~0x3FF);
	val = ptr[(vaddr >> 12) & 0xff];
	if((val & 0x3) == 0)return NULL;
	if((val & 0x2) == 0)
	{
		if(remaining_pagesize)*remaining_pagesize = 0x10000 - (vaddr & 0xFFFF);

		return (u8*)((val & ~0xFFFF) | (vaddr & 0xFFFF));
	}

	if(remaining_pagesize)*remaining_pagesize = 0x1000 - (vaddr & 0xFFF);

	return (u8*)((val & ~0xFFF) | (vaddr & 0xFFF));
}

/*u32 *patch_mmutables(u64 procname, u32 patch_permissions, u32 num)
{
	u32 pos=0, pos2=0;
	//u32 *wram = (u32*)0x1FF80000;
	u32 *kprocess;
	u32 *mmutable = NULL;
	u32 *mmutableL2;
	u32 *page_physaddr = 0;

	kprocess = get_kprocessptr(procname, num, 0);
	if(kprocess==NULL)return NULL;

	mmutable = (u32*)(kprocess[0x54>>2] - 0xd0000000);

	for(pos=1; pos<0x10; pos++)
	{
		if(mmutable[pos]==0)break;

		mmutableL2 = (u32*)((mmutable[pos] >> 10) << 10);

		for(pos2=0; pos2<0x100; pos2++)
		{
			if(mmutableL2[pos2]==0)break;

			if(page_physaddr==0)page_physaddr = (u32*)((mmutableL2[pos2] >> 12) << 12);
			if(patch_permissions)mmutableL2[pos2] |= 0x30;//Set permissions to usr-RW/priv-RW.
		}
	}

	if(patch_permissions)
	{
		kprocess[(28+8 + 0)>>2] |= 0x0101;//Set the TLB invalidation flags used when doing a process context switch.(CPUID0+CPUID1)
	}

	return page_physaddr;
}*/

void writepatch_arm11kernel_kernelpanicbkpt(u32 *ptr, u32 size)
{
	u32 pos, i;

	pos = 0;
	while(size)
	{
		if(ptr[pos] == 0xffff9004 && ptr[pos+1] == 0x010000ff)//Locate the kernelpanic() function(s) via the .pool data. Note that older kernel versions had two kernelpanic() functions.
		{
			for(i=0; i<(0x400/4); i++)
			{
				if(ptr[pos-i] == 0xe92d4010)//"push {r4, lr}"
				{
					//The actual start of the function is this instruction, immediately before the push instruction: "ldr r0, [pc, <offset>]"
					ptr[pos-i-1] = 0xE1200070;//"bkpt #0"
					break;
				}
			}
		}

		pos++;
		size-= 4;
	}
}

