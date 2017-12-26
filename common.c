
//=============================================
// ��������� ��������
//=============================================

//��������� ������ int
void atom_set_state(volatile int *s, int val){
	_asm
	{
		mov     eax, val
		mov     esi, s
		lock    xchg    eax, DWORD PTR[esi]
	}
}

//������
int atom_get_state(volatile int *s) {
	int a, b;

	do {
		a = *s;
		b = *s;
	} while (a != b);

	return a;
}

//���������
void atom_inc(volatile int *num)
{
	_asm
	{
		mov     esi, num
		lock    inc     DWORD PTR[esi]
	};
}

//���������
void atom_dec(volatile int *num)
{
	_asm
	{               mov     esi, num
		lock    dec     DWORD PTR[esi]
	};
}

//�����
int atom_xchg(volatile int *m, int val)
{
	_asm
	{
		mov     eax, val
		mov     esi, m
		lock    xchg    eax, DWORD PTR[esi]
		mov     val, eax
	}
	return val;
}

//����������
void atom_add(volatile int *num, int val)
{
	_asm
	{               mov     esi, num
		mov     eax, val
		lock    add     DWORD PTR[esi], eax
	};
}

//���������
void atom_sub(volatile int *num, int val)
{
	_asm
	{               mov     esi, num
		mov     eax, val
		lock    sub     DWORD PTR[esi], eax
	};
}

//------------------------------------------------------------------------------



