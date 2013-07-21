/*
 * vm.c
 *
 *  Created on: 2013年7月18日
 *      Author: liutos
 */
#include <assert.h>
#include <stdlib.h>

#include "object.h"
#include "prims.h"
#include "type.h"

lt *walk_in_env(lt *env, int n) {
  while (n-- > 0)
    env = pair_tail(env);
  return env;
}

lisp_object_t *find_in_frame(lisp_object_t *bindings, int j) {
  assert(ispair(bindings) || isvector(bindings));
  if (ispair(bindings)) {
    bindings = lt_raw_nthtail(bindings, j);
    return pair_head(bindings);
  } else if (isvector(bindings)) {
    return vector_value(bindings)[j];
  }
  fprintf(stdout, "Impossible!!! variable not found!\n");
  exit(1);
}

lisp_object_t *locate_var(lisp_object_t *env, int i, int j) {
  env = walk_in_env(env, i);
  return find_in_frame(pair_head(env), j);
}

lisp_object_t *raw_vector_ref(lisp_object_t *vector, int index) {
  assert(isvector(vector));
  return vector_value(vector)[index];
}

void set_in_frame(lisp_object_t *bindings, int j, lisp_object_t *value) {
  assert(ispair(bindings) || isvector(bindings));
  if (ispair(bindings)) {
    bindings = lt_raw_nthtail(bindings, j);
    pair_head(bindings) = value;
    return;
  } else if (isvector(bindings)) {
    vector_value(bindings)[j] = value;
    return;
  }
  fprintf(stdout, "Impossible!!! variable not found!\n");
  exit(1);
}

void set_local_var(lisp_object_t *env, int i, int j, lisp_object_t *value) {
  env = walk_in_env(env, i);
  set_in_frame(pair_head(env), j, value);
}

/* TODO: Exception signaling and handling. */
pub lisp_object_t *run_by_llam(lisp_object_t *func) {
#define _arg(N) vlast(stack, primitive_arity(func) - N)
#define _arg1 _arg(1)
#define _arg2 _arg(2)
#define _arg3 _arg(3)
#define move_stack() vector_last(stack) -= primitive_arity(func)
#define vlast(v, n) lt_vector_last_nth(v, make_fixnum(n))

  assert(isfunction(func));
  int pc = 0;
  int throw_exception = TRUE;
  lisp_object_t *stack = make_vector(10);
  lisp_object_t *code = function_code(func);
  lisp_object_t *env = null_env;
  lisp_object_t *return_stack = the_empty_list;
  while (pc < vector_length(code)) {
    lisp_object_t *ins = raw_vector_ref(code, pc);
    switch (opcode_type(ins)) {
      case ARGS: {
        lisp_object_t *args = make_vector(fixnum_value(op_args_arity(ins)));
        for (int i = fixnum_value(op_args_arity(ins)) - 1; i >= 0; i--) {
          lisp_object_t *arg = lt_vector_pop(stack);
          vector_value(args)[i] = arg;
        }
        env = make_pair(args, env);
      }
        break;
      case CALL: {
        lisp_object_t *func = lt_vector_pop(stack);
        assert(isfunction(func));
        lisp_object_t *retaddr = make_retaddr(code, env, pc, throw_exception);
        return_stack = make_pair(retaddr, return_stack);
        code = function_code(func);
        env = function_env(func);
        pc = -1;
        throw_exception = TRUE;
      }
        break;
      case CATCH:
        throw_exception = FALSE;
        break;
      case CONST:
        lt_vector_push(stack, op_const_value(ins));
        break;
      case FJUMP:
        if (isfalse(lt_vector_pop(stack)))
          pc = fixnum_value(op_fjump_label(ins)) - 1;
        break;
      case FN: {
        lisp_object_t *func = op_fn_func(ins);
        func = make_function(env, the_empty_list, function_code(func));
        lt_vector_push(stack, func);
      }
        break;
      case GSET: {
        lisp_object_t *value = vlast(stack, 0);
        lisp_object_t *var = op_gset_var(ins);
        symbol_value(var) = value;
      }
        break;
      case GVAR: {
        lisp_object_t *sym = op_gvar_var(ins);
        if (symbol_value(sym) == the_undef) {
          fprintf(stdout, "Undefined global variable: %s\n", symbol_name(sym));
          exit(1);
        }
        lt_vector_push(stack, symbol_value(sym));
      }
        break;
      case JUMP: pc = fixnum_value(op_jump_label(ins)) - 1; break;
      case LSET: {
        lisp_object_t *value = vlast(stack, 0);
        lisp_object_t *i = op_lset_i(ins);
        lisp_object_t *j = op_lset_j(ins);
        set_local_var(env, fixnum_value(i), fixnum_value(j), value);
      }
        break;
      case LVAR: {
        lisp_object_t *i = op_lvar_i(ins);
        lisp_object_t *j = op_lvar_j(ins);
        lisp_object_t *value = locate_var(env, fixnum_value(i), fixnum_value(j));
        lt_vector_push(stack, value);
      }
        break;
      case MACRO_FN: {
        lisp_object_t *func = op_macro_func(ins);
        func = make_function(env, the_empty_list, function_code(ins));
        lt_vector_push(stack, make_macro(func, env));
      }
      	break;
      case POP:
        lt_vector_pop(stack);
        break;
      case PRIM: {
        lisp_object_t *func = lt_vector_pop(stack);
        lisp_object_t *val = NULL;
        assert(isprimitive(func));
        switch (primitive_arity(func)) {
          case 0:
            val = ((f0)primitive_func(func))();
            break;
          case 1:
            val = ((f1)primitive_func(func))(_arg1);
            break;
          case 2:
            val = ((f2)primitive_func(func))(_arg1, _arg2);
            break;
          case 3:
            val = ((f3)primitive_func(func))(_arg1, _arg2, _arg3);
            break;
          default :
            fprintf(stdout, "Primitive function with arity %d is not supported\n", primitive_arity(func));
            exit(1);
        }
        move_stack();
        lt_vector_push(stack, val);
//        When the primitive function's execution is finished, they will put the return
//        value at the top of stack. If this return value is a signaled exception, and the
//        local variable `throw_exception' is false, it means the last primitive function
//        was called within a `try-with' block. Therefore, the exception object, as a
//        return value, should be left at the top of stack, as the return value, and it
//        will be used by the expandsion code of `try-with' block, in other word, CATCH
//        by the language.
        if (is_signaled(vlast(stack, 0)) && throw_exception)
          goto return_label;
      }
        break;
      case RETURN: {
        return_label:
        if (isnull(return_stack))
          break;
        lisp_object_t *retaddr = pair_head(return_stack);
        return_stack = pair_tail(return_stack);
        code = retaddr_code(retaddr);
        env = retaddr_env(retaddr);
        pc = retaddr_pc(retaddr);
        throw_exception = retaddr_throw_flag(retaddr);
      }
        break;
      default :
        fprintf(stdout, "In run_by_llam --- Invalid opcode %d\n", type_of(ins));
        exit(1);
    }
    pc++;
  }
  assert(isfalse(lt_is_vector_empty(stack)));
  return vlast(stack, 0);
}