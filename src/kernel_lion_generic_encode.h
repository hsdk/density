/*
 * Centaurean Density
 *
 * Copyright (c) 2015, Guillaume Voirin
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *     * Redistributions of source code must retain the above copyright
 *       notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above copyright
 *       notice, this list of conditions and the following disclaimer in the
 *       documentation and/or other materials provided with the distribution.
 *     * Neither the name of Centaurean nor the
 *       names of its contributors may be used to endorse or promote products
 *       derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY
 * DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * 6/03/15 01:08
 *
 * --------------
 * Lion algorithm
 * --------------
 *
 * Author(s)
 * Guillaume Voirin (https://github.com/gpnuma)
 *
 * Description
 * Multiform compression algorithm
 */

DENSITY_FORCE_INLINE DENSITY_KERNEL_ENCODE_STATE GENERIC_NAME(density_lion_encode_) (density_memory_teleport *restrict in, density_memory_location *restrict out, density_lion_encode_state *restrict state) {
    DENSITY_KERNEL_ENCODE_STATE returnState;
    uint32_t hash;
    uint64_t chunk;
    density_byte *pointerOutBefore;
    density_memory_location *readMemoryLocation;

    // Dispatch
    switch (state->process) {
        case DENSITY_LION_ENCODE_PROCESS_PREPARE_NEW_BLOCK:
            goto prepare_new_block;
        case DENSITY_LION_ENCODE_PROCESS_READ_CHUNK:
            goto read_chunk;
        case DENSITY_LION_ENCODE_PROCESS_CHECK_SIGNATURE_STATE:
            goto check_signature_state;
        default:
            return DENSITY_KERNEL_ENCODE_STATE_ERROR;
    }

    // Prepare new block
    prepare_new_block:
    if ((returnState = density_lion_encode_prepare_new_block(state)))
        return exitProcess(state, DENSITY_LION_ENCODE_PROCESS_PREPARE_NEW_BLOCK, returnState);

    check_signature_state:
    if (DENSITY_LION_MAXIMUM_COMPRESSED_UNIT_SIZE > out->available_bytes)
        return DENSITY_KERNEL_ENCODE_STATE_STALL_ON_OUTPUT;
    if ((returnState = density_lion_encode_check_state(state)))
        return exitProcess(state, DENSITY_LION_ENCODE_PROCESS_CHECK_SIGNATURE_STATE, returnState);

    // Try to read a complete chunk unit
    read_chunk:
    pointerOutBefore = out->pointer;
    if (!(readMemoryLocation = density_memory_teleport_read(in, DENSITY_LION_ENCODE_PROCESS_UNIT_SIZE)))
#ifdef DENSITY_LION_ENCODE_CONTINUE
        return exitProcess(state, DENSITY_LION_ENCODE_PROCESS_READ_CHUNK, DENSITY_KERNEL_ENCODE_STATE_STALL_ON_INPUT);
#else
        goto step_by_step;
#endif

    // Chunk was read properly, process
    density_lion_encode_process_unit(&chunk, readMemoryLocation, out, &hash, state);
    readMemoryLocation->available_bytes -= DENSITY_LION_ENCODE_PROCESS_UNIT_SIZE;
#ifndef DENSITY_LION_ENCODE_CONTINUE
    goto exit;

    // Read step by step
    step_by_step:
    while (state->shift != density_bitsizeof(density_lion_signature) && (readMemoryLocation = density_memory_teleport_read(in, sizeof(uint32_t)))) {
        density_lion_encode_kernel(out, &hash, *(uint32_t *) (readMemoryLocation->pointer), state);
        readMemoryLocation->pointer += sizeof(uint32_t);
        readMemoryLocation->available_bytes -= sizeof(uint32_t);
    }
    exit:
#endif
    out->available_bytes -= (out->pointer - pointerOutBefore);

    // New loop
#ifdef DENSITY_LION_ENCODE_CONTINUE
    goto check_signature_state;
#else
    if (density_memory_teleport_available_bytes(in) >= sizeof(uint32_t))
        goto check_signature_state;

    // Marker for decode loop exit
    const density_lion_entropy_code code = density_lion_form_model_get_encoding(&state->formData, DENSITY_LION_FORM_CHUNK_DICTIONARY_A);
    density_lion_encode_push_to_signature(out, state, code.value, code.bitLength);

    // Copy the remaining bytes
    *(state->signature) = state->proximitySignature;
    density_memory_teleport_copy_remaining(in, out);

    return DENSITY_KERNEL_ENCODE_STATE_READY;
#endif
}