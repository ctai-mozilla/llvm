; RUN: llc < %s -mtriple armv8 -mattr=+neon | FileCheck %s
; RUN: llc < %s -mtriple armv8 -mattr=+neon,+v8fp -enable-unsafe-fp-math | FileCheck %s --check-prefix=CHECK-FAST

define <4 x float> @vmaxnmq(<4 x float>* %A, <4 x float>* %B) nounwind {
; CHECK: vmaxnmq
; CHECK: vmaxnm.f32 q{{[0-9]+}}, q{{[0-9]+}}, q{{[0-9]+}}
  %tmp1 = load <4 x float>* %A
  %tmp2 = load <4 x float>* %B
  %tmp3 = call <4 x float> @llvm.arm.neon.vmaxnm.v4f32(<4 x float> %tmp1, <4 x float> %tmp2)
  ret <4 x float> %tmp3
}

define <2 x float> @vmaxnmd(<2 x float>* %A, <2 x float>* %B) nounwind {
; CHECK: vmaxnmd
; CHECK: vmaxnm.f32 d{{[0-9]+}}, d{{[0-9]+}}, d{{[0-9]+}}
  %tmp1 = load <2 x float>* %A
  %tmp2 = load <2 x float>* %B
  %tmp3 = call <2 x float> @llvm.arm.neon.vmaxnm.v2f32(<2 x float> %tmp1, <2 x float> %tmp2)
  ret <2 x float> %tmp3
}

define <4 x float> @vminnmq(<4 x float>* %A, <4 x float>* %B) nounwind {
; CHECK: vminnmq
; CHECK: vminnm.f32 q{{[0-9]+}}, q{{[0-9]+}}, q{{[0-9]+}}
  %tmp1 = load <4 x float>* %A
  %tmp2 = load <4 x float>* %B
  %tmp3 = call <4 x float> @llvm.arm.neon.vminnm.v4f32(<4 x float> %tmp1, <4 x float> %tmp2)
  ret <4 x float> %tmp3
}

define <2 x float> @vminnmd(<2 x float>* %A, <2 x float>* %B) nounwind {
; CHECK: vminnmd
; CHECK: vminnm.f32 d{{[0-9]+}}, d{{[0-9]+}}, d{{[0-9]+}}
  %tmp1 = load <2 x float>* %A
  %tmp2 = load <2 x float>* %B
  %tmp3 = call <2 x float> @llvm.arm.neon.vminnm.v2f32(<2 x float> %tmp1, <2 x float> %tmp2)
  ret <2 x float> %tmp3
}

define float @v8fp_vminnm_o(float %a, float %b) {
; CHECK-FAST: v8fp_vminnm_o
; CHECK-FAST-NOT: vcmp
; CHECK-FAST: vminnm.f32
; CHECK: v8fp_vminnm_o
; CHECK-NOT: vminnm.f32
  %cmp = fcmp olt float %a, %b
  %cond = select i1 %cmp, float %a, float %b
  ret float %cond
}

define float @v8fp_vminnm_u(float %a, float %b) {
; CHECK-FAST: v8fp_vminnm_u
; CHECK-FAST-NOT: vcmp
; CHECK-FAST: vminnm.f32
; CHECK: v8fp_vminnm_u
; CHECK-NOT: vminnm.f32
  %cmp = fcmp ult float %a, %b
  %cond = select i1 %cmp, float %a, float %b
  ret float %cond
}

define float @v8fp_vmaxnm_o(float %a, float %b) {
; CHECK-FAST: v8fp_vmaxnm_o
; CHECK-FAST-NOT: vcmp
; CHECK-FAST: vmaxnm.f32
; CHECK: v8fp_vmaxnm_o
; CHECK-NOT: vmaxnm.f32
  %cmp = fcmp ogt float %a, %b
  %cond = select i1 %cmp, float %a, float %b
  ret float %cond
}

define float @v8fp_vmaxnm_u(float %a, float %b) {
; CHECK-FAST: v8fp_vmaxnm_u
; CHECK-FAST-NOT: vcmp
; CHECK-FAST: vmaxnm.f32
; CHECK: v8fp_vmaxnm_u
; CHECK-NOT: vmaxnm.f32
  %cmp = fcmp ugt float %a, %b
  %cond = select i1 %cmp, float %a, float %b
  ret float %cond
}


declare <4 x float> @llvm.arm.neon.vminnm.v4f32(<4 x float>, <4 x float>) nounwind readnone
declare <2 x float> @llvm.arm.neon.vminnm.v2f32(<2 x float>, <2 x float>) nounwind readnone
declare <4 x float> @llvm.arm.neon.vmaxnm.v4f32(<4 x float>, <4 x float>) nounwind readnone
declare <2 x float> @llvm.arm.neon.vmaxnm.v2f32(<2 x float>, <2 x float>) nounwind readnone
