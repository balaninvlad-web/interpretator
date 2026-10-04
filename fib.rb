require_relative 'dsl'

prog do

  li    r0, 0            #(константа-ноль)
  li    r1, 0            # a = 0  (F(0))
  li    r2, 1            # b = 1  (F(1))
  li    r3, 10           # счётчик n = 10  (ищем F(10) = 55)

  label :loop

  beq   r3, r0, :done    # n == 0 выйти из цикла
  add   r4, r1, r2       # t = a + b
  add   r1, r2, r0       # a = b
  add   r2, r4, r0       # b = t
  addi  r3, r3, -1       # n = n - 1
  j     :loop

  label :done
  hlt                    # LI X8, 93 + SYSCALL (exit)
end