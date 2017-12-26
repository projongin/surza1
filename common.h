#pragma once


//=============================================
// атомарные операции
//=============================================

//атомарная запись int
void atom_set_state(volatile int *s, int val);

//чтение
int atom_get_state(volatile int *s);

//инкремент
void atom_inc(volatile int *num);

//декремент
void atom_dec(volatile int *num);

//обмен
int atom_xchg(volatile int *m, int val);

//добавление
void atom_add(volatile int *num, int val);

//вычитание
void atom_sub(volatile int *num, int val);

//------------------------------------------------------------------------------


