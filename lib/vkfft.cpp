#include "vkFFT.h"
#include "ShaderLang.h"
#include "sigpack/sigpack.h"
#include "utils_VkFFT.h"

const uint64_t kVkNumBuf = 1;
const uint64_t sample_size = sizeof(std::complex<float>);
static VkGPU vkGPU = {};
static VkFFTConfiguration vkConfiguration = {};
static VkDeviceMemory *vkBufferDeviceMemory = NULL;
static VkFFTApplication vkApp = {};
static VkFFTLaunchParams vkLaunchParams = {};
static VkBuffer stagingBuffer = {0};
static VkDeviceMemory stagingBufferMemory = {0};
static uint64_t fftBufferSize = 0;
static uint64_t stagingBufferSize = 0;

VkFFTResult _transferDataFromCPU(char *cpu_arr, std::size_t transferSize) {
  VkResult res = VK_SUCCESS;
  VkBuffer *buffer = &vkConfiguration.buffer[0];
  char *data;
  res = vkMapMemory(vkGPU.device, stagingBufferMemory, 0, transferSize, 0,
                    (void **)&data);
  if (res != VK_SUCCESS)
    return VKFFT_ERROR_MALLOC_FAILED;
  memcpy(data, cpu_arr, transferSize);
  vkUnmapMemory(vkGPU.device, stagingBufferMemory);
  VkCommandBufferAllocateInfo commandBufferAllocateInfo = {
      VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
  commandBufferAllocateInfo.commandPool = vkGPU.commandPool;
  commandBufferAllocateInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
  commandBufferAllocateInfo.commandBufferCount = 1;
  VkCommandBuffer commandBuffer = {0};
  res = vkAllocateCommandBuffers(vkGPU.device, &commandBufferAllocateInfo,
                                 &commandBuffer);
  if (res != VK_SUCCESS)
    return VKFFT_ERROR_FAILED_TO_ALLOCATE_COMMAND_BUFFERS;
  VkCommandBufferBeginInfo commandBufferBeginInfo = {
      VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
  commandBufferBeginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
  res = vkBeginCommandBuffer(commandBuffer, &commandBufferBeginInfo);
  if (res != VK_SUCCESS)
    return VKFFT_ERROR_FAILED_TO_BEGIN_COMMAND_BUFFER;
  VkBufferCopy copyRegion = {0};
  copyRegion.srcOffset = 0;
  copyRegion.dstOffset = 0;
  copyRegion.size = transferSize;
  vkCmdCopyBuffer(commandBuffer, stagingBuffer, buffer[0], 1, &copyRegion);
  res = vkEndCommandBuffer(commandBuffer);
  if (res != VK_SUCCESS)
    return VKFFT_ERROR_FAILED_TO_END_COMMAND_BUFFER;
  VkSubmitInfo submitInfo = {VK_STRUCTURE_TYPE_SUBMIT_INFO};
  submitInfo.commandBufferCount = 1;
  submitInfo.pCommandBuffers = &commandBuffer;
  res = vkQueueSubmit(vkGPU.queue, 1, &submitInfo, vkGPU.fence);
  if (res != VK_SUCCESS)
    return VKFFT_ERROR_FAILED_TO_SUBMIT_QUEUE;
  res = vkWaitForFences(vkGPU.device, 1, &vkGPU.fence, VK_TRUE, 100000000000);
  if (res != VK_SUCCESS)
    return VKFFT_ERROR_FAILED_TO_WAIT_FOR_FENCES;
  res = vkResetFences(vkGPU.device, 1, &vkGPU.fence);
  if (res != VK_SUCCESS)
    return VKFFT_ERROR_FAILED_TO_RESET_FENCES;
  vkFreeCommandBuffers(vkGPU.device, vkGPU.commandPool, 1, &commandBuffer);
  return VKFFT_SUCCESS;
}

VkFFTResult _transferDataToCPU(char *cpu_arr, std::size_t transferSize) {
  // a function that transfers data from the GPU to the CPU using staging
  // buffer, because the GPU memory is not host-coherent
  VkResult res = VK_SUCCESS;
  VkBuffer *buffer = &vkConfiguration.buffer[0];
  VkCommandBufferAllocateInfo commandBufferAllocateInfo = {
      VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
  commandBufferAllocateInfo.commandPool = vkGPU.commandPool;
  commandBufferAllocateInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
  commandBufferAllocateInfo.commandBufferCount = 1;
  VkCommandBuffer commandBuffer = {0};
  res = vkAllocateCommandBuffers(vkGPU.device, &commandBufferAllocateInfo,
                                 &commandBuffer);
  if (res != VK_SUCCESS)
    return VKFFT_ERROR_FAILED_TO_ALLOCATE_COMMAND_BUFFERS;
  VkCommandBufferBeginInfo commandBufferBeginInfo = {
      VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
  commandBufferBeginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
  res = vkBeginCommandBuffer(commandBuffer, &commandBufferBeginInfo);
  if (res != VK_SUCCESS)
    return VKFFT_ERROR_FAILED_TO_BEGIN_COMMAND_BUFFER;
  VkBufferCopy copyRegion = {0};
  copyRegion.srcOffset = 0;
  copyRegion.dstOffset = 0;
  copyRegion.size = transferSize;
  vkCmdCopyBuffer(commandBuffer, buffer[0], stagingBuffer, 1, &copyRegion);
  res = vkEndCommandBuffer(commandBuffer);
  if (res != VK_SUCCESS)
    return VKFFT_ERROR_FAILED_TO_END_COMMAND_BUFFER;
  VkSubmitInfo submitInfo = {VK_STRUCTURE_TYPE_SUBMIT_INFO};
  submitInfo.commandBufferCount = 1;
  submitInfo.pCommandBuffers = &commandBuffer;
  res = vkQueueSubmit(vkGPU.queue, 1, &submitInfo, vkGPU.fence);
  if (res != VK_SUCCESS)
    return VKFFT_ERROR_FAILED_TO_SUBMIT_QUEUE;
  res = vkWaitForFences(vkGPU.device, 1, &vkGPU.fence, VK_TRUE, 100000000000);
  if (res != VK_SUCCESS)
    return VKFFT_ERROR_FAILED_TO_WAIT_FOR_FENCES;
  res = vkResetFences(vkGPU.device, 1, &vkGPU.fence);
  if (res != VK_SUCCESS)
    return VKFFT_ERROR_FAILED_TO_RESET_FENCES;
  vkFreeCommandBuffers(vkGPU.device, vkGPU.commandPool, 1, &commandBuffer);
  char *data;
  res = vkMapMemory(vkGPU.device, stagingBufferMemory, 0, transferSize, 0,
                    (void **)&data);
  if (res != VK_SUCCESS)
    return VKFFT_ERROR_MALLOC_FAILED;
  memcpy(cpu_arr, data, transferSize);
  vkUnmapMemory(vkGPU.device, stagingBufferMemory);
  return VKFFT_SUCCESS;
}

int64_t init_vkfft(std::size_t batches, std::size_t nfft,
                   std::size_t sample_id) {
  vkGPU.enableValidationLayers = 0;

  VkResult res = VK_SUCCESS;
  res = createInstance(&vkGPU, sample_id);
  if (res) {
    std::cerr << "VKFFT_ERROR_FAILED_TO_CREATE_INSTANCE" << std::endl;
    return VKFFT_ERROR_FAILED_TO_CREATE_INSTANCE;
  }
  res = setupDebugMessenger(&vkGPU);
  if (res != 0) {
    // printf("Debug messenger creation failed, error code: %" PRIu64 "\n",
    // res);
    return VKFFT_ERROR_FAILED_TO_SETUP_DEBUG_MESSENGER;
  }
  res = findPhysicalDevice(&vkGPU);
  if (res != 0) {
    // printf("Physical device not found, error code: %" PRIu64 "\n", res);
    return VKFFT_ERROR_FAILED_TO_FIND_PHYSICAL_DEVICE;
  }
  res = createDevice(&vkGPU, sample_id);
  if (res != 0) {
    // printf("Device creation failed, error code: %" PRIu64 "\n", res);
    return VKFFT_ERROR_FAILED_TO_CREATE_DEVICE;
  }
  res = createFence(&vkGPU);
  if (res != 0) {
    // printf("Fence creation failed, error code: %" PRIu64 "\n", res);
    return VKFFT_ERROR_FAILED_TO_CREATE_FENCE;
  }
  res = createCommandPool(&vkGPU);
  if (res != 0) {
    // printf("Fence creation failed, error code: %" PRIu64 "\n", res);
    return VKFFT_ERROR_FAILED_TO_CREATE_COMMAND_POOL;
  }
  vkGetPhysicalDeviceProperties(vkGPU.physicalDevice,
                                &vkGPU.physicalDeviceProperties);
  vkGetPhysicalDeviceMemoryProperties(vkGPU.physicalDevice,
                                      &vkGPU.physicalDeviceMemoryProperties);

  glslang_initialize_process(); // compiler can be initialized before VkFFT

  vkConfiguration.FFTdim = 1;
  vkConfiguration.size[0] = nfft;
  vkConfiguration.size[1] = 1;
  vkConfiguration.size[2] = 1;
  vkConfiguration.device = &vkGPU.device;
  vkConfiguration.queue = &vkGPU.queue;
  vkConfiguration.fence = &vkGPU.fence;
  vkConfiguration.commandPool = &vkGPU.commandPool;
  vkConfiguration.physicalDevice = &vkGPU.physicalDevice;
  vkConfiguration.isCompilerInitialized = true;
  vkConfiguration.doublePrecision = false;
  vkConfiguration.numberBatches = batches;
  fftBufferSize = nfft * sample_size;
  stagingBufferSize = fftBufferSize * vkConfiguration.numberBatches;

  std::cerr << "using vkFFT batch size " << vkConfiguration.numberBatches
            << " on " << vkGPU.physicalDeviceProperties.deviceName << std::endl;

  vkConfiguration.bufferSize =
      (uint64_t *)aligned_alloc(sizeof(uint64_t), sizeof(uint64_t) * kVkNumBuf);
  if (!vkConfiguration.bufferSize)
    return VKFFT_ERROR_MALLOC_FAILED;

  const size_t buffer_size_f =
      vkConfiguration.size[0] * vkConfiguration.size[1] *
      vkConfiguration.size[2] * vkConfiguration.numberBatches / kVkNumBuf;
  for (uint64_t i = 0; i < kVkNumBuf; ++i) {
    vkConfiguration.bufferSize[i] = (uint64_t)sample_size * buffer_size_f;
  }

  vkConfiguration.bufferNum = kVkNumBuf;

  vkConfiguration.buffer =
      (VkBuffer *)aligned_alloc(sample_size, kVkNumBuf * sizeof(VkBuffer));
  if (!vkConfiguration.buffer)
    return VKFFT_ERROR_MALLOC_FAILED;
  vkBufferDeviceMemory = (VkDeviceMemory *)aligned_alloc(
      sample_size, kVkNumBuf * sizeof(VkDeviceMemory));
  if (!vkBufferDeviceMemory)
    return VKFFT_ERROR_MALLOC_FAILED;

  for (uint64_t i = 0; i < kVkNumBuf; ++i) {
    vkConfiguration.buffer[i] = {};
    vkBufferDeviceMemory[i] = {};
    VkFFTResult resFFT = allocateBuffer(
        &vkGPU, &vkConfiguration.buffer[i], &vkBufferDeviceMemory[i],
        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT |
            VK_BUFFER_USAGE_TRANSFER_DST_BIT,
        VK_MEMORY_HEAP_DEVICE_LOCAL_BIT, vkConfiguration.bufferSize[i]);
    if (resFFT != VKFFT_SUCCESS)
      return VKFFT_ERROR_MALLOC_FAILED;
  }

  vkLaunchParams.buffer = vkConfiguration.buffer;

  VkFFTResult resFFT = initializeVkFFT(&vkApp, vkConfiguration);
  if (resFFT != VKFFT_SUCCESS)
    return resFFT;

  resFFT = allocateBuffer(&vkGPU, &stagingBuffer, &stagingBufferMemory,
                          VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                          VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                              VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                          stagingBufferSize);
  if (resFFT != VKFFT_SUCCESS)
    return VKFFT_ERROR_MALLOC_FAILED;

  return VKFFT_SUCCESS;
}

void free_vkfft() {
  vkDestroyBuffer(vkGPU.device, stagingBuffer, NULL);
  vkFreeMemory(vkGPU.device, stagingBufferMemory, NULL);
  for (uint64_t i = 0; i < kVkNumBuf; i++) {
    vkDestroyBuffer(vkGPU.device, vkConfiguration.buffer[i], NULL);
    vkFreeMemory(vkGPU.device, vkBufferDeviceMemory[i], NULL);
  }
  free(vkConfiguration.buffer);
  free(vkBufferDeviceMemory);
  free(vkConfiguration.bufferSize);
  deleteVkFFT(&vkApp);
  vkDestroyFence(vkGPU.device, vkGPU.fence, NULL);
  vkDestroyCommandPool(vkGPU.device, vkGPU.commandPool, NULL);
  vkDestroyDevice(vkGPU.device, NULL);
  DestroyDebugUtilsMessengerEXT(&vkGPU, NULL);
  vkDestroyInstance(vkGPU.instance, NULL);
  glslang_finalize_process();
}

void vkfft_specgram_offload(arma::cx_fmat &Pw_in, arma::cx_fmat &Pw) {
  size_t row_size = Pw_in.n_rows * sample_size;

  // TODO: offload windowing.
  for (arma::uword k = 0; k < Pw_in.n_cols;
       k += vkConfiguration.numberBatches) {
    size_t offset = k * row_size;
    // TODO: retain overlap buffer from previous spectrum() call.
    uint64_t buffer_size = std::min(vkConfiguration.bufferSize[0],
                                    (uint64_t)((Pw_in.n_cols - k) * row_size));
    _transferDataFromCPU(((char *)Pw_in.memptr()) + offset, buffer_size);
    performVulkanFFT(&vkGPU, &vkApp, &vkLaunchParams, -1, 1);
    _transferDataToCPU(((char *)Pw.memptr()) + offset, buffer_size);
  }
}
