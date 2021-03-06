/***************************************************************************
 *
 *  @license
 *  Copyright (C) Codeplay Software Limited
 *  Licensed under the Apache License, Version 2.0 (the "License");
 *  you may not use this file except in compliance with the License.
 *  You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 *  For your convenience, a copy of the License has been included in this
 *  repository.
 *
 *  Unless required by applicable law or agreed to in writing, software
 *  distributed under the License is distributed on an "AS IS" BASIS,
 *  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *  See the License for the specific language governing permissions and
 *  limitations under the License.
 *
 *  SYCL-BLAS: BLAS implementation using SYCL
 *
 *  @filename blas3_interface_sycl.hpp
 *
 **************************************************************************/

#ifndef BLAS3_INTERFACE_SYCL_GEMM_HPP
#define BLAS3_INTERFACE_SYCL_GEMM_HPP

#include <algorithm>
#include <cctype>
#include <cmath>
#include <iostream>
#include <stdexcept>
#include <vector>

#include <executors/executor_sycl.hpp>
#include <operations/blas3_trees.hpp>

namespace blas {

/*!
 * @brief Select the correct transpose version of GemmFactory, depending on the
 *        runtime values of transpose.
 */
template <int WgSize, bool DoubleBuffer, bool ConflictA, bool ConflictB,
          int ClSize, typename TileT, typename ExecutorType, typename T,
          typename IndexType>
cl::sycl::event _select_gemm(Executor<ExecutorType>& ex, bool _TransA,
                             bool _TransB, IndexType _M, IndexType _N,
                             IndexType _K, T _alpha, T* _A, IndexType _lda,
                             T* _B, IndexType _ldb, T _beta, T* _C,
                             IndexType _ldc) {
  cl::sycl::event event;
  using RHS =
      matrix_view<T, typename Executor<ExecutorType>::template ContainerT<T>>;
  auto a_container = ex.get_buffer(_A);
  RHS buffer_a(a_container, _M, _K, 0, _lda, ex.get_offset(_A));
  auto b_container = ex.get_buffer(_B);
  RHS buffer_b(b_container, _K, _N, 0, _ldb, ex.get_offset(_B));
  auto c_container = ex.get_buffer(_C);
  RHS buffer_c(c_container, _M, _N, 0, _ldc, ex.get_offset(_C));
#define ENABLE_GEMM_TRANSPOSE(_trans_a, _trans_b)                              \
  if (_TransA == _trans_a && _TransB == _trans_b) {                            \
    if (ex.has_local_memory()) {                                               \
      auto gemm = make_gemm<DoubleBuffer, ConflictA, ConflictB, ClSize, TileT, \
                            _trans_a, _trans_b>(buffer_a, buffer_b, buffer_c,  \
                                                T(_alpha), T(_beta));          \
      event = ex.gemm_executor(gemm);                                          \
    } else {                                                                   \
      auto gemm = make_gemm_no_local_mem<WgSize, _trans_a, _trans_b>(          \
          buffer_a, buffer_b, buffer_c, T(_alpha), T(_beta));                  \
      event = ex.gemm_executor(gemm);                                          \
    }                                                                          \
    return event;                                                              \
  }

  const bool NoTrans = false;
  const bool Trans = true;

  ENABLE_GEMM_TRANSPOSE(NoTrans, NoTrans);
  ENABLE_GEMM_TRANSPOSE(Trans, NoTrans);
  ENABLE_GEMM_TRANSPOSE(NoTrans, Trans);
  ENABLE_GEMM_TRANSPOSE(Trans, Trans);

#undef ENABLE_GEMM_TRANSPOSE
  return event;
}

/*!
 * @brief This is a top-level wrapper for GemmFactory, which provides a
 *        "standard" BLAS gemm interface.
 *
 * See netlib.org/blas for details.
 */
template <typename ExecutorType, typename T, typename IndexType>
cl::sycl::event _gemm(Executor<ExecutorType>& ex, char _TransA, char _TransB,
                      IndexType _M, IndexType _N, IndexType _K, T _alpha, T* _A,
                      IndexType _lda, T* _B, IndexType _ldb, T _beta, T* _C,
                      IndexType _ldc) {
  _TransA = tolower(_TransA);
  _TransB = tolower(_TransB);

  if (_TransA != 'n' && _TransA != 't' && _TransA != 'c') {
    throw std::invalid_argument("invalid _TransA");
  } else if (_TransB != 'n' && _TransB != 't' && _TransB != 'c') {
    throw std::invalid_argument("invalid _TransB");
  }

  bool _TrA = _TransA != 'n';
  bool _TrB = _TransB != 'n';
#define BIND_DATA_SIZE(_m, _n, _k) if (_M == (_m) && _N == (_n) && _K == (_k))

#define BIND_DEFAULT

#define TO_TPARAMS(_wg, _db, _tir, _tic, _twr, _twc)                       \
  {                                                                        \
    return _select_gemm<_wg, _db, false, false, 64,                        \
                        Tile<_tir, _tic, _twr, _twc>>(                     \
        ex, _TrA, _TrB, _M, _N, _K, _alpha, _A, _lda, _B, _ldb, _beta, _C, \
        _ldc);                                                             \
  }

  if (ex.get_device_type() == Queue_Interface<SYCL>::device_type::INTELGPU) {
    BIND_DATA_SIZE(1024, 4096, 1024) TO_TPARAMS(128, false, 4, 4, 16, 16);
    BIND_DATA_SIZE(10, 1024, 1024) TO_TPARAMS(128, false, 2, 2, 8, 8);
    BIND_DEFAULT TO_TPARAMS(128, false, 8, 8, 8, 8);
  } else {
    BIND_DATA_SIZE(10, 1024, 1024) TO_TPARAMS(128, true, 1, 1, 16, 16);
    BIND_DEFAULT TO_TPARAMS(128, false, 8, 8, 16, 16);
  }

#undef BIND_DATA_SIZE
#undef BIND_DEFAULT
#undef TO_TPARAMS
}

}  // namespace blas

#endif  // BLAS3_INTERFACE_SYCL_HPP
