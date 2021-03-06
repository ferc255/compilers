#include <libgccjit.h>

#include <ctype.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "rpn.h"


#define ATTEMPTS_PER_TEST 5
#define MAX_STATEMENT_SIZE 20 // < 21


typedef long double (*fn_type)(long double);


void push_to_stack(gcc_jit_context* ctxt, gcc_jit_block* block, 
                   gcc_jit_rvalue* one, gcc_jit_lvalue* stack, 
                   gcc_jit_lvalue* stack_size, gcc_jit_rvalue* number)
{
    gcc_jit_block_add_assignment(block, NULL,
        gcc_jit_context_new_array_access(ctxt, NULL,
            gcc_jit_lvalue_as_rvalue(stack),
            gcc_jit_lvalue_as_rvalue(stack_size)),
        number
    );
            
    gcc_jit_block_add_assignment_op(block, NULL,
        stack_size,
        GCC_JIT_BINARY_OP_PLUS,
        one
    );
}


void get_from_stack(gcc_jit_context* ctxt, gcc_jit_block* block, 
                    gcc_jit_rvalue* one, gcc_jit_lvalue* stack,
                    gcc_jit_lvalue* stack_size, gcc_jit_lvalue* number)
{
    gcc_jit_block_add_assignment_op(block, NULL,
        stack_size,
        GCC_JIT_BINARY_OP_MINUS,
        one
    );
        
    gcc_jit_block_add_assignment(block, NULL,
        number,
        gcc_jit_lvalue_as_rvalue(gcc_jit_context_new_array_access(ctxt, NULL,
            gcc_jit_lvalue_as_rvalue(stack),
            gcc_jit_lvalue_as_rvalue(stack_size))
        )
    );
}


void operation_block(gcc_jit_context* ctxt, gcc_jit_block* block,
                     gcc_jit_rvalue* one,
                     gcc_jit_lvalue* stack, gcc_jit_lvalue* stack_size,
                     gcc_jit_lvalue* A, gcc_jit_lvalue* B,
                     enum gcc_jit_binary_op operation)
{
    get_from_stack(ctxt, block, one, stack, stack_size, B);
    get_from_stack(ctxt, block, one, stack, stack_size, A);
    
    gcc_jit_block_add_assignment_op(block, NULL,
        A,
        operation,
        gcc_jit_lvalue_as_rvalue(B)
    );
    
    push_to_stack(ctxt, block, one, stack, stack_size, 
        gcc_jit_lvalue_as_rvalue(A));
}


void create_code(gcc_jit_context* ctxt, int len,
                 char* statement[][MAX_STACK_DEPTH],
                  int* st_size)
{   
    gcc_jit_type* int_type = 
        gcc_jit_context_get_type(ctxt, GCC_JIT_TYPE_INT);
    gcc_jit_type* Lf_type =
        gcc_jit_context_get_type(ctxt, GCC_JIT_TYPE_LONG_DOUBLE);
    gcc_jit_type* stack_type = 
        gcc_jit_context_new_array_type(ctxt, NULL, Lf_type, MAX_STACK_DEPTH);
    
    gcc_jit_param* x_val = 
        gcc_jit_context_new_param(ctxt, NULL, Lf_type, "x_val");
        
    gcc_jit_function* func = 
        gcc_jit_context_new_function(ctxt, NULL,
                                     GCC_JIT_FUNCTION_EXPORTED,
                                     Lf_type,
                                     "calculate",
                                     1, &x_val,
                                     0);
                                    
    gcc_jit_block* block = 
        gcc_jit_function_new_block(func, "block");
        
    gcc_jit_lvalue* stack = gcc_jit_function_new_local(func, NULL,
                                                       stack_type, "stack");
    gcc_jit_lvalue* stack_size = gcc_jit_function_new_local(func, NULL,
        int_type, "stack_size");
    gcc_jit_block_add_assignment(block, NULL, stack_size, 
                                 gcc_jit_context_zero(ctxt, int_type));
    gcc_jit_rvalue* x = 
        gcc_jit_param_as_rvalue(x_val);
    gcc_jit_lvalue* A = gcc_jit_function_new_local(func, NULL, Lf_type, "A");
    gcc_jit_lvalue* B = gcc_jit_function_new_local(func, NULL, Lf_type, "B");
        
    int i;
    for (i = 0; i < st_size[len]; i++)
    {
        if (statement[len][i][0] == '+')
        {
            operation_block(ctxt, block, gcc_jit_context_one(ctxt, int_type), 
                            stack, stack_size, A, B,
                            GCC_JIT_BINARY_OP_PLUS);
        }
        else if (statement[len][i][0] == '-')
        {
            operation_block(ctxt, block, gcc_jit_context_one(ctxt, int_type), 
                            stack, stack_size, A, B,
                            GCC_JIT_BINARY_OP_MINUS);
        }
        else if (statement[len][i][0] == '*')
        {
            operation_block(ctxt, block, gcc_jit_context_one(ctxt, int_type), 
                            stack, stack_size, A, B,
                            GCC_JIT_BINARY_OP_MULT);
        }
        else if (statement[len][i][0] == '/')
        {
            operation_block(ctxt, block, gcc_jit_context_one(ctxt, int_type), 
                            stack, stack_size, A, B,
                            GCC_JIT_BINARY_OP_DIVIDE);
        }
        else if (statement[len][i][0] == 'x')
        {
            push_to_stack(ctxt, block, gcc_jit_context_one(ctxt, int_type), 
                          stack, stack_size, x);
        }
        else
        {
            gcc_jit_rvalue* number = 
                gcc_jit_context_new_rvalue_from_double(ctxt, Lf_type, 
                    atof(statement[len][i]));
            push_to_stack(ctxt, block, gcc_jit_context_one(ctxt, int_type), 
                          stack, stack_size, number);
        }
    }
    
    gcc_jit_block_end_with_return(block, NULL,
        gcc_jit_lvalue_as_rvalue(
            gcc_jit_context_new_array_access(ctxt, NULL,
                gcc_jit_lvalue_as_rvalue(stack),
                gcc_jit_context_zero(ctxt, int_type))
        )
    );
}


fn_type get_jit_function(int len,
                         char* statement[][MAX_STACK_DEPTH],
                         int* st_size)
{
    gcc_jit_context* ctxt = gcc_jit_context_acquire();
    if (!ctxt)
    {
        printf("Bad ctxt\n");
        exit(0);
    }
    
    create_code(ctxt, len, statement, st_size);
    
    gcc_jit_result* result = gcc_jit_context_compile(ctxt);
    if (!result)
    {
        printf("Bad result\n");
        exit(0);
    }
    gcc_jit_context_release(ctxt);
    
    void* fn_ptr = gcc_jit_result_get_code(result, "calculate");
    if (!fn_ptr)
    {
        printf("Bad fn_ptr\n");
        exit(0);
    }
    
    return (fn_type)fn_ptr;
}



void measure_time(char* statement[][MAX_STACK_DEPTH], int* st_size)
{
    double timer[MAX_STATEMENT_SIZE];
    int len;
    for (len = 0; len < MAX_STATEMENT_SIZE; len++)
    {
        printf("[len = %d]\n", len + 1);
        
        fn_type calculate = get_jit_function(len, statement, st_size);

        double sum = 0;
        int attempt;
        for (attempt = 0; attempt < ATTEMPTS_PER_TEST; attempt++)
        {
            double start_time = clock();
            
            int x;
            for (x = 0; x < 1e7; x++)
            {
                calculate(x);
            }
            
            double elapsed = ((clock() - start_time) / CLOCKS_PER_SEC);
            sum += elapsed;
            
            printf("Elapsed time: %.3f sec\n", elapsed);
        }
        timer[len] = sum / attempt;
        
        printf("_________________________________\n");
        printf("Average elapsed time: %.3f sec\n", timer[len]);
    }
    
    printf("\n");
    int i;
    for (i = 0; i < MAX_STATEMENT_SIZE; i++)
    {
        printf("gccjit,%d,%.3lf\n", i + 1, timer[i]);
    }
    printf("\n");
}


void test_for_12(char* statement[MAX_STATEMENT_SIZE][MAX_STACK_DEPTH],
                 int* st_size)
{
    int i;
    for (i = 0; i < MAX_STATEMENT_SIZE; i++)
    {
        fn_type calc = get_jit_function(i, statement, st_size);
        printf("%Lf\n", calc(12));
    }
}


int main()
{
    char* input[] =
    {
        #include "statements.h"
    };
    int st_size[MAX_STATEMENT_SIZE] = {};
    char* statement[MAX_STATEMENT_SIZE][MAX_STACK_DEPTH];
   
    fill_statement(input, statement, st_size, MAX_STATEMENT_SIZE);

    
    measure_time(statement, st_size);
    
    //fn_type calc = get_jit_function(19, statement, st_size);
    //printf("%Lf\n", calc(12));

    //test_for_12(statement, st_size);
    
    return 0;
}
