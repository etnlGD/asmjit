// [AsmJit]
// Complete x86/x64 JIT and Remote Assembler for C++.
//
// [License]
// Zlib - See LICENSE.md file in the package.

// [Export]
#define ASMJIT_EXPORTS

// [Guard]
#include "../asmjit_build.h"
#if defined(ASMJIT_BUILD_X86) && !defined(ASMJIT_DISABLE_BUILDER)

// [Dependencies]
#include "../x86/x86inst.h"
#include "../x86/x86operand.h"
#include "../x86/x86ssetoavxpass_p.h"

// [Api-Begin]
#include "../asmjit_apibegin.h"

namespace asmjit {

// ============================================================================
// [asmjit::X86SseToAvxPass - Construction / Destruction]
// ============================================================================

X86SseToAvxPass::X86SseToAvxPass() noexcept
  : CBPass("X86SseToAvxPass"),
    _translated(false) {}

// ============================================================================
// [asmjit::X86SseToAvxPass - Run]
// ============================================================================

Error X86SseToAvxPass::run(Zone* zone) noexcept {
  ZoneHeap heap(zone);
  ZoneStack<CBInst*> insts;
  ASMJIT_PROPAGATE(insts.init(&heap));

  // Probe loop. Let's add all nodes that can be converted to `insts` list and
  // let's fail if nodes are not consistent or there is node that cannot be
  // translated to AVX (uses MMX regs for example, or uses SSE4A instruction).
  CBNode* node_ = cb()->getFirstNode();

  while (node_) {
    if (node_->getType() == CBNode::kNodeInst) {
      CBInst* inst = node_->as<CBInst>();
      uint32_t instId = inst->getInstId();

      // Skip invalid and high-level instructions; we don't care about them here.
      if (!X86Inst::isDefinedId(instId))
        continue;

      // Skip non-SSE instructions.
      const X86Inst& instData = X86Inst::getInst(instId);
      if (!instData.isVec() || instData.isVex() || instData.isEvex())
        continue;

      uint32_t opCount = inst->getOpCount();
      uint32_t regTypes = probeRegs(inst->getOpArray(), opCount);

      // Skip instructions that don't use XMM registers.
      if (!(regTypes & kProbeXmm))
        continue;

      if (!(regTypes & kProbeMmx)) {
        // This is the common case.
        const X86Inst::SseToAvxData& sseToAvx = instData.getSseToAvxData();

        switch (sseToAvx.getMode()) {
          case X86Inst::kSseToAvxNone:
            // Cannot translate.
            return kErrorOk;

          case X86Inst::kSseToAvxMove:
            break;

          case X86Inst::kSseToAvxMoveIfMem:
          case X86Inst::kSseToAvxExtend:
            // Cannot translate if the instruction is malformed.
            if (ASMJIT_UNLIKELY(opCount < 1 || opCount > 3))
              return kErrorOk;
            break;

          case X86Inst::kSseToAvxBlend:
            // Cannot translate if the instruction is malformed.
            if (ASMJIT_UNLIKELY(opCount < 2 || opCount > 3))
              return kErrorOk;
            break;
        }
      }
      else {
        // If this instruction uses MMX register it means that it's a conversion
        // between MMX and XMM, this cannot be directly translated to AVX as
        // there is no AVX instruction that can address MMX register(s).
        return kErrorOk;
      }

      ASMJIT_PROPAGATE(insts.append(inst));
    }

    node_ = node_->getNext();
  }

  // Second loop - patch all nodes we added to `insts` to use AVX instead of SSE.
  // At this moment we know that patching should not cause any performance issues
  // and all instructions added to `insts` are patchable.
  while (!insts.isEmpty()) {
    CBInst* inst = insts.popFirst();

    uint32_t instId = inst->getInstId();
    ASMJIT_ASSERT(X86Inst::isDefinedId(instId));

    const X86Inst& instData = X86Inst::getInst(instId);
    const X86Inst::SseToAvxData& sseToAvx = instData.getSseToAvxData();

    uint32_t i;
    uint32_t opCount = inst->getOpCount();

    switch (sseToAvx.getMode()) {
      case X86Inst::kSseToAvxNone:
        // That should not happen.
        break;

      case X86Inst::kSseToAvxMove:
        // Nothing to patch...
        break;

      case X86Inst::kSseToAvxMoveIfMem:
        if (inst->hasMemOp())
          break;
        goto Extend;

      case X86Inst::kSseToAvxBlend:
        // Translate [xmmA, xmmB/m128, <xmm0>] to [xmmA, xmmA, xmmB/m128, xmm0].
        if (opCount == 2)
          inst->_opArray[opCount++] = x86::xmm0;
        goto Extend;

      case X86Inst::kSseToAvxExtend:
Extend:
        for (i = opCount; i > 0; i--)
          inst->setOp(i, inst->getOp(i - 1));
        inst->setOpCount(opCount + 1);
        break;
    }

    instId = static_cast<uint32_t>(static_cast<int32_t>(instId) + sseToAvx.getDelta());
    ASMJIT_ASSERT(X86Inst::isDefinedId(instId));

    inst->setInstId(instId);
  }

  _translated = true;
  return kErrorOk;
}

} // asmjit namespace

// [Api-End]
#include "../asmjit_apiend.h"

// [Guard]
#endif // ASMJIT_BUILD_X86 && !ASMJIT_DISABLE_BUILDER
