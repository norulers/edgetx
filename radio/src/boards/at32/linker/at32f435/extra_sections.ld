PROVIDE(_heap_start = _eram);

/* Reserve heap space in RAM */
._user_heap(NOLOAD) :
{
  . = ALIGN(4);
  . = . + MIN_HEAP_SIZE;
  . = ALIGN(4);
} > REGION_RAM

/* Reserve stack space in RAM */
.stack(NOLOAD) :
{
  . = ALIGN(4);
  . = . + MAIN_STACK_SIZE;
  . = ALIGN(4);
} > RAM
