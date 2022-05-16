/*
 * Copyright (c) 2022, NVIDIA CORPORATION.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "mg_test_utils.h" /* RUN_TEST */

#include <cugraph_c/algorithms.h>
#include <cugraph_c/graph.h>

#include <math.h>

typedef int32_t vertex_t;
typedef int32_t edge_t;
typedef float weight_t;

int generic_triangle_count_test(const cugraph_resource_handle_t* handle,
                                vertex_t* h_src,
                                vertex_t* h_dst,
                                weight_t* h_wgt,
                                vertex_t* h_verts,
                                edge_t* h_result,
                                size_t num_vertices,
                                size_t num_edges,
                                size_t num_results,
                                bool_t store_transposed)
{
  int test_ret_value = 0;

  cugraph_error_code_t ret_code = CUGRAPH_SUCCESS;
  cugraph_error_t* ret_error;

  cugraph_graph_t* p_graph                              = NULL;
  cugraph_triangle_count_result_t* p_result             = NULL;
  cugraph_type_erased_device_array_t* p_start           = NULL;
  cugraph_type_erased_device_array_view_t* p_start_view = NULL;

  ret_code = create_mg_test_graph(
    handle, h_src, h_dst, h_wgt, num_edges, store_transposed, FALSE, &p_graph, &ret_error);

  TEST_ASSERT(test_ret_value, ret_code == CUGRAPH_SUCCESS, "create_mg_test_graph failed.");

  if (h_verts != NULL) {
    ret_code =
      cugraph_type_erased_device_array_create(handle, num_results, INT32, &p_start, &ret_error);
    TEST_ASSERT(test_ret_value, ret_code == CUGRAPH_SUCCESS, "p_start create failed.");

    p_start_view = cugraph_type_erased_device_array_view(p_start);

    ret_code = cugraph_type_erased_device_array_view_copy_from_host(
      handle, p_start_view, (byte_t*)h_verts, &ret_error);
    TEST_ASSERT(test_ret_value, ret_code == CUGRAPH_SUCCESS, "src copy_from_host failed.");
  }

  ret_code = cugraph_triangle_count(handle, p_graph, p_start_view, FALSE, &p_result, &ret_error);
#if 1
  TEST_ASSERT(test_ret_value, ret_code != CUGRAPH_SUCCESS, cugraph_error_message(ret_error));
  TEST_ALWAYS_ASSERT(ret_code != CUGRAPH_SUCCESS, "cugraph_triangle_count expected to fail.");
#else
  TEST_ASSERT(test_ret_value, ret_code == CUGRAPH_SUCCESS, cugraph_error_message(ret_error));
  TEST_ALWAYS_ASSERT(ret_code == CUGRAPH_SUCCESS, "cugraph_triangle_count failed.");

  if (test_ret_value == 0) {
    cugraph_type_erased_device_array_view_t* vertices;
    cugraph_type_erased_device_array_view_t* counts;

    vertices = cugraph_triangle_count_result_get_vertices(p_result);
    counts   = cugraph_triangle_count_result_get_counts(p_result);

    TEST_ASSERT(test_ret_value,
                cugraph_type_erased_device_array_view_size(vertices) == num_results,
                "invalid number of results");

    vertex_t h_vertices[num_results];
    edge_t h_counts[num_results];

    ret_code = cugraph_type_erased_device_array_view_copy_to_host(
      handle, (byte_t*)h_vertices, vertices, &ret_error);
    TEST_ASSERT(test_ret_value, ret_code == CUGRAPH_SUCCESS, "copy_to_host failed.");

    ret_code = cugraph_type_erased_device_array_view_copy_to_host(
      handle, (byte_t*)h_counts, counts, &ret_error);
    TEST_ASSERT(test_ret_value, ret_code == CUGRAPH_SUCCESS, "copy_to_host failed.");

    for (int i = 0; (i < num_vertices) && (test_ret_value == 0); ++i) {
      TEST_ASSERT(
        test_ret_value, h_result[h_vertices[i]] == h_counts[i], "counts results don't match");
    }

    cugraph_triangle_count_result_free(p_result);
  }
#endif

  cugraph_mg_graph_free(p_graph);
  cugraph_error_free(ret_error);

  return test_ret_value;
}

int test_triangle_count(const cugraph_resource_handle_t* handle)
{
  size_t num_edges    = 8;
  size_t num_vertices = 6;
  size_t num_results  = 3;

  vertex_t h_src[]   = {0, 1, 1, 2, 2, 2, 3, 4};
  vertex_t h_dst[]   = {1, 3, 4, 0, 1, 3, 5, 5};
  weight_t h_wgt[]   = {0.1f, 2.1f, 1.1f, 5.1f, 3.1f, 4.1f, 7.2f, 3.2f};
  vertex_t h_verts[] = {0, 1, 2};
  edge_t h_result[]  = {0, 0, 0};

  // Triangle Count wants store_transposed = FALSE
  return generic_triangle_count_test(
    handle, h_src, h_dst, h_wgt, h_verts, h_result, num_vertices, num_edges, num_results, FALSE);
}

/******************************************************************************/

int main(int argc, char** argv)
{
  // Set up MPI:
  int comm_rank;
  int comm_size;
  int num_gpus_per_node;
  cudaError_t status;
  int mpi_status;
  int result                        = 0;
  cugraph_resource_handle_t* handle = NULL;
  cugraph_error_t* ret_error;
  cugraph_error_code_t ret_code = CUGRAPH_SUCCESS;
  int prows                     = 1;

  C_MPI_TRY(MPI_Init(&argc, &argv));
  C_MPI_TRY(MPI_Comm_rank(MPI_COMM_WORLD, &comm_rank));
  C_MPI_TRY(MPI_Comm_size(MPI_COMM_WORLD, &comm_size));
  C_CUDA_TRY(cudaGetDeviceCount(&num_gpus_per_node));
  C_CUDA_TRY(cudaSetDevice(comm_rank % num_gpus_per_node));

  void* raft_handle = create_raft_handle(prows);
  handle            = cugraph_create_resource_handle(raft_handle);

  if (result == 0) {
    result |= RUN_MG_TEST(test_triangle_count, handle);

    cugraph_free_resource_handle(handle);
  }

  free_raft_handle(raft_handle);

  C_MPI_TRY(MPI_Finalize());

  return result;
}