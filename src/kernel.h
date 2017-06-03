#pragma once

#include "kernel/syscalls.h"
#include "assertions.h"
#include "dynamic_libs/os_functions.h"
#include "tcp_gecko.h"
#include "utils/logger.h"

unsigned char *kernelCopyBuffer[sizeof(int)];

// TODO Variable size, not hard-coded
unsigned char *kernelCopyBufferOld[DATA_BUFFER_SIZE];

void kernelCopyData(unsigned char *destinationBuffer, unsigned char *sourceBuffer, unsigned int length) {
	if (length > DATA_BUFFER_SIZE) {
		OSFatal("Kernel copy buffer size exceeded");
	}

	memcpy(kernelCopyBufferOld, sourceBuffer, length);
	SC0x25_KernelCopyData((unsigned int) OSEffectiveToPhysical(destinationBuffer), (unsigned int) &kernelCopyBufferOld, length);
	DCFlushRange(destinationBuffer, (u32) length);
}

void kernelCopyInt(unsigned char *destinationBuffer, unsigned char *sourceBuffer, unsigned int length) {
	memcpy(kernelCopyBuffer, sourceBuffer, length);
	unsigned int destinationAddress = (unsigned int) OSEffectiveToPhysical(destinationBuffer);
	SC0x25_KernelCopyData(destinationAddress, (unsigned int) &kernelCopyBuffer, length);
	DCFlushRange(destinationBuffer, (u32) length);
}

void writeKernelMemory(const void *address, uint32_t value) {
	((int *) kernelCopyBuffer)[0] = value;
	kernelCopyInt((unsigned char *) address, (unsigned char *) kernelCopyBuffer, sizeof(int));
}

int readKernelMemory(const void *address) {
	// For addresses in that range use Chadderz' function to avoid crashing
	if (address > (const void *) 0xF0000000) {
		log_print("Using Chadderz' kern_read()...\n");
		return kern_read(address);
	}

	log_print("Using dimok's kernelCopy()...\n");
	unsigned char *readBuffer[sizeof(int)];
	kernelCopyInt((unsigned char *) readBuffer, (unsigned char *) address, sizeof(int));

	return ((int *) readBuffer)[0];
}

#define KERNEL_COPY_SOURCE_ADDRESS 0x10100000

int kernelCopyService(int argc, void *argv) {
	while (true) {
		// Read the destination address from the source address
		int destinationAddress = *(int *) KERNEL_COPY_SOURCE_ADDRESS;

		// Avoid crashing
		if (OSIsAddressValid((const void *) destinationAddress)) {
			// Perform memory copy
			unsigned char *valueBuffer = (unsigned char *) (KERNEL_COPY_SOURCE_ADDRESS + 4);
			kernelCopyInt((unsigned char *) destinationAddress, valueBuffer, 4);

			// "Consume" address and value for synchronization with the code handler for instance
			*(int *) KERNEL_COPY_SOURCE_ADDRESS = 0;
			*(((int *) KERNEL_COPY_SOURCE_ADDRESS) + 1) = 0;
		}
	}
}

void startKernelCopyService() {
	unsigned int stack = (unsigned int) memalign(0x40, 0x100);
	ASSERT_ALLOCATED(stack, "Kernel copy thread stack")
	stack += 0x100;
	void *thread = memalign(0x40, 0x1000);
	ASSERT_ALLOCATED(thread, "Kernel copy thread")

	int status = OSCreateThread(thread, kernelCopyService, 1, NULL, (u32) stack + sizeof(stack), sizeof(stack), 31,
								OS_THREAD_ATTR_AFFINITY_CORE1 | OS_THREAD_ATTR_PINNED_AFFINITY | OS_THREAD_ATTR_DETACH);
	ASSERT_INTEGER(status, 1, "Creating kernel copy thread")
	// OSSetThreadName(thread, "Kernel Copier");
	OSResumeThread(thread);
}

#define MINIMUM_KERNEL_COMPARE_LENGTH 4
#define KERNEL_MEMORY_COMPARE_STEP_SIZE 1

int kernelMemoryCompare(const void *sourceBuffer,
						const void *destinationBuffer,
						int length) {
	if (length < MINIMUM_KERNEL_COMPARE_LENGTH) {
		ASSERT_MINIMUM_HOLDS(length, MINIMUM_KERNEL_COMPARE_LENGTH, "length");
	}

	bool loopEntered = false;

	while (kern_read(sourceBuffer) == kern_read(destinationBuffer)) {
		loopEntered = true;
		sourceBuffer = (char *) sourceBuffer + KERNEL_MEMORY_COMPARE_STEP_SIZE;
		destinationBuffer = (char *) destinationBuffer + KERNEL_MEMORY_COMPARE_STEP_SIZE;
		length -= KERNEL_MEMORY_COMPARE_STEP_SIZE;

		if (length <= MINIMUM_KERNEL_COMPARE_LENGTH - 1) {
			break;
		}
	}

	if (loopEntered) {
		sourceBuffer -= KERNEL_MEMORY_COMPARE_STEP_SIZE;
		destinationBuffer -= KERNEL_MEMORY_COMPARE_STEP_SIZE;
	}

	return kern_read(sourceBuffer) - kern_read(destinationBuffer);
}