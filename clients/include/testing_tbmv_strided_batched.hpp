/* ************************************************************************
 * Copyright 2016-2020 Advanced Micro Devices, Inc.
 *
 * ************************************************************************ */

#include <fstream>
#include <iostream>
#include <stdlib.h>
#include <vector>

#include "cblas_interface.h"
#include "flops.h"
#include "hipblas.hpp"
#include "norm.h"
#include "unit.h"
#include "utility.h"

using namespace std;

/* ============================================================================================ */

template <typename T>
hipblasStatus_t testing_tbmv_strided_batched(Arguments argus)
{
    int    M            = argus.M;
    int    K            = argus.K;
    int    lda          = argus.lda;
    int    incx         = argus.incx;
    double stride_scale = argus.stride_scale;
    int    batch_count  = argus.batch_count;

    int stride_A = lda * M * stride_scale;
    int stride_x = M * incx * stride_scale;

    int A_size = stride_A * batch_count;
    int X_size = stride_x * batch_count;

    hipblasFillMode_t  uplo   = char2hipblas_fill(argus.uplo_option);
    hipblasOperation_t transA = char2hipblas_operation(argus.transA_option);
    hipblasDiagType_t  diag   = char2hipblas_diagonal(argus.diag_option);

    hipblasStatus_t status = HIPBLAS_STATUS_SUCCESS;

    // argument sanity check, quick return if input parameters are invalid before allocating invalid
    // memory
    if(M < 0 || K < 0 || lda < M || incx == 0 || batch_count < 0)
    {
        return HIPBLAS_STATUS_INVALID_VALUE;
    }

    // Naming: dK is in GPU (device) memory. hK is in CPU (host) memory
    host_vector<T> hA(A_size);
    host_vector<T> hx(X_size);
    host_vector<T> hres(X_size);

    device_vector<T> dA(A_size);
    device_vector<T> dx(X_size);

    double gpu_time_used, cpu_time_used;
    double hipblasGflops, cblas_gflops, hipblasBandwidth;
    double rocblas_error;

    T alpha = (T)argus.alpha;
    T beta  = (T)argus.beta;

    hipblasHandle_t handle;
    hipblasCreate(&handle);

    // Initial Data on CPU
    srand(1);
    hipblas_init<T>(hA, M, M, lda, stride_A, batch_count);
    hipblas_init<T>(hx, 1, M, incx, stride_x, batch_count);
    hres = hx;

    // copy data from CPU to device
    hipMemcpy(dA, hA.data(), sizeof(T) * A_size, hipMemcpyHostToDevice);
    hipMemcpy(dx, hx.data(), sizeof(T) * X_size, hipMemcpyHostToDevice);

    /* =====================================================================
           ROCBLAS
    =================================================================== */
    for(int iter = 0; iter < 1; iter++)
    {
        status = hipblasTbmvStridedBatched<T>(
            handle, uplo, transA, diag, M, K, dA, lda, stride_A, dx, incx, stride_x, batch_count);

        if(status != HIPBLAS_STATUS_SUCCESS)
        {
            // here in cuda
            hipblasDestroy(handle);
            return status;
        }
    }

    // copy output from device to CPU
    hipMemcpy(hres.data(), dx, sizeof(T) * X_size, hipMemcpyDeviceToHost);

    if(argus.unit_check)
    {
        /* =====================================================================
           CPU BLAS
        =================================================================== */

        for(int b = 0; b < batch_count; b++)
        {
            cblas_tbmv<T>(uplo,
                          transA,
                          diag,
                          M,
                          K,
                          hA.data() + b * stride_A,
                          lda,
                          hx.data() + b * stride_x,
                          incx);
        }

        // enable unit check, notice unit check is not invasive, but norm check is,
        // unit check and norm check can not be interchanged their order
        if(argus.unit_check)
        {
            unit_check_general<T>(1, M, batch_count, incx, stride_x, hx, hres);
        }
    }

    hipblasDestroy(handle);
    return HIPBLAS_STATUS_SUCCESS;
}
