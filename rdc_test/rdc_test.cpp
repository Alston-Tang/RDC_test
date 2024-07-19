#include <stdio.h>

#include <rdc/rdc.h>
#include <unistd.h>

#include <map>
#include <string>
#include <vector>

std::vector<rdc_field_t> test_fields = {};
std::map<rdc_field_t, std::string> test_fields_names = {};

size_t INTERVAL = 1;

#define ADD_TEST_FIELD(name)         \
  {                                  \
    test_fields.push_back(name);     \
    test_fields_names[name] = #name; \
  }

struct PerDeviceMetrics {
  std::map<int, int64_t> last_acc_tx;
  std::map<int, int64_t> last_acc_rx;
  std::map<int, double> xgmi_tx;
  std::map<int, double> xgmi_rx;
};

std::map<int, PerDeviceMetrics> metrics;

void update_xgmi(const rdc_field_value& value, int dev, int duration_sec) {
  std::map<int, int64_t>* last_acc = nullptr;
  std::map<int, double>* rate = nullptr;
  int remote_dev = 0;
  if (value.field_id >= RDC_FI_XGMI_0_READ_KB &&
      value.field_id <= RDC_FI_XGMI_7_READ_KB) {
    rate = &metrics[dev].xgmi_rx;
    last_acc = &metrics[dev].last_acc_rx;
    remote_dev = value.field_id - RDC_FI_XGMI_0_READ_KB;
  } else if (
      value.field_id >= RDC_FI_XGMI_0_WRITE_KB &&
      value.field_id <= RDC_FI_XGMI_7_WRITE_KB) {
    rate = &metrics[dev].xgmi_tx;
    last_acc = &metrics[dev].last_acc_tx;
    remote_dev = value.field_id - RDC_FI_XGMI_0_WRITE_KB;
  }

  (*rate)[remote_dev] =
      static_cast<double>((value.value.l_int - (*last_acc)[remote_dev])) *
      1024 / duration_sec;
  (*last_acc)[remote_dev] = value.value.l_int;
}

int main() {
  // ADD_TEST_FIELD(RDC_FI_GPU_COUNT);
  ADD_TEST_FIELD(RDC_FI_DEV_NAME);
  // ADD_TEST_FIELD(RDC_FI_GPU_CLOCK);
  // ADD_TEST_FIELD(RDC_FI_MEM_CLOCK);
  // ADD_TEST_FIELD(RDC_FI_MEMORY_TEMP);
  // ADD_TEST_FIELD(RDC_FI_GPU_TEMP);
  // ADD_TEST_FIELD(RDC_FI_POWER_USAGE);
  // ADD_TEST_FIELD(RDC_FI_PCIE_BANDWIDTH);
  // ADD_TEST_FIELD(RDC_FI_GPU_UTIL);
  // ADD_TEST_FIELD(RDC_FI_GPU_MEMORY_USAGE);
  // ADD_TEST_FIELD(RDC_FI_GPU_MEMORY_TOTAL);
  // ADD_TEST_FIELD(RDC_FI_ECC_CORRECT_TOTAL);
  // ADD_TEST_FIELD(RDC_FI_ECC_UNCORRECT_TOTAL);
  // ADD_TEST_FIELD(RDC_FI_ECC_SDMA_SEC);
  // ADD_TEST_FIELD(RDC_FI_ECC_SDMA_DED);
  // ADD_TEST_FIELD(RDC_FI_ECC_GFX_SEC);
  // ADD_TEST_FIELD(RDC_FI_ECC_GFX_DED);
  // ADD_TEST_FIELD(RDC_FI_ECC_MMHUB_SEC);
  // ADD_TEST_FIELD(RDC_FI_ECC_MMHUB_DED);
  // ADD_TEST_FIELD(RDC_FI_ECC_ATHUB_SEC);
  // ADD_TEST_FIELD(RDC_FI_ECC_ATHUB_DED);

  // ADD_TEST_FIELD(RDC_FI_PROF_CU_UTILIZATION);
  // ADD_TEST_FIELD(RDC_FI_PROF_CU_OCCUPANCY);
  // ADD_TEST_FIELD(RDC_FI_PROF_FLOPS_16);
  // ADD_TEST_FIELD(RDC_FI_PROF_FLOPS_32);
  // ADD_TEST_FIELD(RDC_FI_PROF_FLOPS_64);
  // ADD_TEST_FIELD(RDC_FI_PROF_ACTIVE_CYCLES);
  // ADD_TEST_FIELD(RDC_FI_PROF_ACTIVE_WAVES);
  // ADD_TEST_FIELD(RDC_FI_PROF_ELAPSED_CYCLES);

  ADD_TEST_FIELD(RDC_FI_XGMI_0_READ_KB);
  ADD_TEST_FIELD(RDC_FI_XGMI_0_WRITE_KB);
  ADD_TEST_FIELD(RDC_FI_XGMI_1_READ_KB);
  ADD_TEST_FIELD(RDC_FI_XGMI_1_WRITE_KB);
  ADD_TEST_FIELD(RDC_FI_XGMI_2_READ_KB);
  ADD_TEST_FIELD(RDC_FI_XGMI_2_WRITE_KB);
  ADD_TEST_FIELD(RDC_FI_XGMI_3_READ_KB);
  ADD_TEST_FIELD(RDC_FI_XGMI_3_WRITE_KB);
  ADD_TEST_FIELD(RDC_FI_XGMI_4_READ_KB);
  ADD_TEST_FIELD(RDC_FI_XGMI_4_WRITE_KB);
  ADD_TEST_FIELD(RDC_FI_XGMI_5_READ_KB);
  ADD_TEST_FIELD(RDC_FI_XGMI_5_WRITE_KB);
  ADD_TEST_FIELD(RDC_FI_XGMI_6_READ_KB);
  ADD_TEST_FIELD(RDC_FI_XGMI_6_WRITE_KB);
  ADD_TEST_FIELD(RDC_FI_XGMI_7_READ_KB);
  ADD_TEST_FIELD(RDC_FI_XGMI_7_WRITE_KB);

  std::vector<int> dev_list = {0, 1, 2, 3, 4, 5, 6, 7};

  rdc_handle_t rdc_handle;
  /*
    rdc_status_t rdc_init(uint64_t init_flags)
    Initialize ROCm RDC.

    When called, this initializes internal data structures, including those
    corresponding to sources of information that RDC provides. This must be
    called before rdc_start_embedded() or rdc_connect()

    Parameters
    :
    init_flags – [in] init_flags Bit flags that tell RDC how to initialize.

    Return values
    :
    RDC_ST_OK – is returned upon successful call.
  */
  rdc_status_t result = rdc_init(0);
  if (result != RDC_ST_OK) {
    printf("RDC initialization failed with error %d\n", result);
    return -1;
  }
  /*
    rdc_status_t rdc_start_embedded(rdc_operation_mode_t op_mode, rdc_handle_t
    *p_rdc_handle) Start embedded RDC agent within this process.

    The RDC is loaded as library so that it does not require rdcd daemon. In
    this mode, the user has to periodically call rdc_field_update_all() when
    op_mode is RDC_OPERATION_MODE_MANUAL, which tells RDC to collect the stats.

    Parameters
    :
    op_mode – [in] Operation modes. When RDC_OPERATION_MODE_AUTO, RDC schedules
    background task to collect the stats. When RDC_OPERATION_MODE_MANUAL, the
    user needs to call rdc_field_update_all() periodically.

    p_rdc_handle – [inout] Caller provided pointer to rdc_handle_t. Upon
    successful call, the value will contain the handler for following API calls.

    Return values
    :
    RDC_ST_OK – is returned upon successful call.
  */

  result = rdc_start_embedded(RDC_OPERATION_MODE_AUTO, &rdc_handle);
  if (result != RDC_ST_OK) {
    printf("RDC start failed with error %d\n", result);
    return -1;
  }

  /*
    Create a group contains multiple GPUs.

    This method can create a group contains multiple GPUs. Instead of executing
    an operation separately for each GPU, the RDC group enables the user to
    execute same operation on all the GPUs present in the group as a single API
    call.

    Parameters
    :
    p_rdc_handle – [in] The RDC handler.

    type – [in] The type of the group. RDC_GROUP_DEFAULT includes all the GPUs
    on the node, and RDC_GROUP_EMPTY creates an empty group.

    group_name – [in] The group name specified as NULL terminated C String

    p_rdc_group_id – [inout] Caller provided pointer to rdc_gpu_group_t. Upon
    successful call, the value will contain the group id for following group API
    calls.

    Return values
    :
    RDC_ST_OK – is returned upon successful call.
  */
  rdc_gpu_group_t groupId;
  result =
      rdc_group_gpu_create(rdc_handle, RDC_GROUP_EMPTY, "MyGroup1", &groupId);
  if (result != RDC_ST_OK) {
    printf("RDC group creation failed with error %d\n", result);
    return -1;
  }
  /*
    rdc_status_t rdc_group_gpu_add(rdc_handle_t p_rdc_handle, rdc_gpu_group_t
    group_id, uint32_t gpu_index) Add a GPU to the group.

    This method can add a GPU to the group

    Parameters
    :
    p_rdc_handle – [in] The RDC handler.

    group_id – [in] The group id to which the GPU will be added.

    gpu_index – [in] The GPU index to be added to the group.

    Return values
    :
    RDC_ST_OK – is returned upon successful call.
  */
  for (auto dev : dev_list) {
    printf("adding gpu %d\n", dev);
    result = rdc_group_gpu_add(rdc_handle, groupId, dev); // Add GPU dev
    if (result != RDC_ST_OK) {
      printf("failed to add gpu %d to group %d\n", dev, groupId);
      return -1;
    }
  }

  /*
    rdc_status_t rdc_group_field_create(rdc_handle_t p_rdc_handle, uint32_t
    num_field_ids, rdc_field_t *field_ids, const char *field_group_name,
    rdc_field_grp_t *rdc_field_group_id) create a group of fields

    The user can create a group of fields and perform an operation on a group of
    fields at once.

    Parameters
    :
    p_rdc_handle – [in] The RDC handler.

    num_field_ids – [in] Number of field IDs that are being provided in
    field_ids.

    field_ids – [in] Field IDs to be added to the newly-created field group.

    field_group_name – [in] Unique name for this group of fields.

    rdc_field_group_id – [out] Handle to the newly-created field group

    Return values
    :
    RDC_ST_OK – is returned upon successful call.
  */
  rdc_field_grp_t rdcFieldGroupId;
  result = rdc_group_field_create(
      rdc_handle,
      test_fields.size(),
      test_fields.data(),
      "MyFieldGroup1",
      &rdcFieldGroupId);
  if (result != RDC_ST_OK) {
    printf("RDC field group creation failed with error %d\n", result);
    return -1;
  }

  /*
    rdc_status_t rdc_field_watch(rdc_handle_t p_rdc_handle, rdc_gpu_group_t
    group_id, rdc_field_grp_t field_group_id, uint64_t update_freq, double
    max_keep_age, uint32_t max_keep_samples) Request the RDC start recording
    updates for a given field collection.

    Note that the first update of the field will not occur until the next field
    update cycle. To force a field update cycle, user must call
    rdc_field_update_all(1)

    Parameters
    :
    p_rdc_handle – [in] The RDC handler.

    group_id – [in] The group of GPUs to be watched.

    field_group_id – [in] The collection of fields to record

    update_freq – [in] How often to update fields in usec.

    max_keep_age – [in] How long to keep data for fields in seconds.

    max_keep_samples – [in] Maximum number of samples to keep. 0=no limit.

    Return values
    :
    RDC_ST_OK – is returned upon successful call.
  */
  result = rdc_field_watch(rdc_handle, groupId, rdcFieldGroupId, 1000000, 2, 2);
  if (result != RDC_ST_OK) {
    printf("RDC field watch failed with error %d\n", result);
    return -1;
  }
  /*
    rdc_status_t rdc_field_get_latest_value(rdc_handle_t p_rdc_handle, uint32_t
    gpu_index, rdc_field_t field, rdc_field_value *value) Request a latest
    cached field of a GPU.

    Note that the field can be cached after called rdc_field_watch

    Parameters
    :
    p_rdc_handle – [in] The RDC handler.

    gpu_index – [in] The GPU index.

    field – [in] The field id

    value – [out] The field value got from cache.

    Return values
    :
    RDC_ST_OK – is returned upon successful call.
  */

  while (true) {
    sleep(INTERVAL);
    rdc_field_value value{};
    for (auto dev : dev_list) {
      printf("device %d\n", dev);
      printf("===================================\n");
      for (auto i = 0; i < test_fields.size(); i++) {
        auto field = test_fields[i];
        result = rdc_field_get_latest_value(rdc_handle, dev, field, &value);
        if (result != RDC_ST_OK) {
          printf("RDC field get latest failed with error %d\n", result);
          continue;
        }
        // NOTE rdc_field_get_latest_value() will not set value.status on
        // success.
        if (value.status != RDC_ST_OK) {
          printf("returned value is not RDC_ST_OK\n");
          continue;
        }

        if (value.field_id >= RDC_FI_XGMI_0_READ_KB &&
            value.field_id <= RDC_FI_XGMI_7_WRITE_KB) {
          update_xgmi(value, dev, INTERVAL);
        } else {
          printf("type=%d, updated=%lu\n", value.type, value.ts);
          printf("%s: ", test_fields_names[field].c_str());
          if (value.type == rdc_field_type_t::INTEGER) {
            printf("%ld\n", value.value.l_int);
          } else if (
              value.type == rdc_field_type_t::DOUBLE ||
              value.type == rdc_field_type_t::BLOB) {
            printf("%f\n", value.value.dbl);
          } else if (value.type == rdc_field_type_t::STRING) {
            printf("%s\n", value.value.str);
          }
        }
      }
      if (metrics.contains(dev)) {
        if (metrics.at(dev).xgmi_tx.size() > 0) {
          double xgmi_tx_all = 0;
          for (const auto& [_, val] : metrics.at(dev).xgmi_tx) {
            xgmi_tx_all += val;
          }
          printf("XGMI TX = %lf GB/S\n", xgmi_tx_all / 1024 / 1024 / 1024);
        }
        if (metrics.at(dev).xgmi_rx.size() > 0) {
          double xgmi_rx_all = 0;
          for (const auto& [_, val] : metrics.at(dev).xgmi_rx) {
            xgmi_rx_all += val;
          }
          printf("XGMI RX = %lf GB/S\n", xgmi_rx_all / 1024 / 1024 / 1024);
        }
      }

      printf("\n");
    }
  }
  return 0;
}
