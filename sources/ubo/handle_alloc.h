#ifndef __HANDLE_ALLOC_H__
#define __HANDLE_ALLOC_H__

#include <limits>
#include <stdint.h>

typedef uint8_t handle_t;

static const handle_t invalid_handle_t = UINT8_MAX;

template <handle_t max_handles_t>
struct handle_alloc_t
{
    handle_alloc_t() :
        _num_handles(0),
        _max_handles(max_handles_t)
    {
        reset();
    }

    void reset()
    {
        _num_handles = 0;
        for (handle_t i = 0, num = max_handles_t; i < num; i++)
            _dense[i] = i;
    }


    handle_t alloc()
    {
        if (_num_handles < _max_handles)
        {
            handle_t index = _num_handles;
            _num_handles++;
            handle_t handle = _dense[index];
            _sparse[handle] = index;

            return handle;
        }
        return invalid_handle_t;
    }

    void free(handle_t handle)
    {
        if (handle == invalid_handle_t)
            return;

        handle_t top_index = _num_handles - 1;
        handle_t index = _sparse[handle];

        assert(index != invalid_handle_t);

        // swap top element with removed one
        handle_t temp = _dense[top_index];
        _dense[index] = temp;
        _sparse[temp] = index;

        // save removed one to reuse when alloc
        _dense[top_index] = handle;

        _num_handles--;
    }

    handle_t _num_handles;
    handle_t _max_handles;

    static_assert(max_handles_t > 0, "max_handle_t should be greater than 0");

    handle_t _dense[max_handles_t];  // hold handle values
    handle_t _sparse[max_handles_t]; // index cache
};

#endif // __HANDLE_ALLOC_H__
