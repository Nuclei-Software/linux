/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2021 Western Digital Corporation or its affiliates.
 * Copyright (C) 2022 SiFive
 *
 * Authors:
 *     Atish Patra <atish.patra@wdc.com>
 *     Anup Patel <anup.patel@wdc.com>
 *     Vincent Chen <vincent.chen@sifive.com>
 *     Greentime Hu <greentime.hu@sifive.com>
 */

#ifndef __KVM_VCPU_RISCV_VECTOR_H
#define __KVM_VCPU_RISCV_VECTOR_H

#include <linux/types.h>

struct kvm_cpu_context;

#ifdef CONFIG_VECTOR
void __kvm_riscv_vector_save(struct kvm_cpu_context *context);
void __kvm_riscv_vector_restore(struct kvm_cpu_context *context);
void kvm_riscv_vcpu_vector_reset(struct kvm_vcpu *vcpu);
void kvm_riscv_vcpu_guest_vector_save(struct kvm_cpu_context *cntx,
				      unsigned long isa);
void kvm_riscv_vcpu_guest_vector_restore(struct kvm_cpu_context *cntx,
					 unsigned long isa);
void kvm_riscv_vcpu_host_vector_save(struct kvm_cpu_context *cntx);
void kvm_riscv_vcpu_host_vector_restore(struct kvm_cpu_context *cntx);
void kvm_riscv_vcpu_free_vector_context(struct kvm_vcpu *vcpu);
#else
static inline void kvm_riscv_vcpu_vector_reset(struct kvm_vcpu *vcpu)
{
}

static inline void kvm_riscv_vcpu_guest_vector_save(struct kvm_cpu_context *cntx,
						    unsigned long isa)
{
}

static inline void kvm_riscv_vcpu_guest_vector_restore(struct kvm_cpu_context *cntx,
						       unsigned long isa)
{
}

static inline void kvm_riscv_vcpu_host_vector_save(struct kvm_cpu_context *cntx)
{
}

static inline void kvm_riscv_vcpu_host_vector_restore(struct kvm_cpu_context *cntx)
{
}

static inline void kvm_riscv_vcpu_free_vector_context(struct kvm_vcpu *vcpu)
{
}
#endif

int kvm_riscv_vcpu_get_reg_vector(struct kvm_vcpu *vcpu,
				  const struct kvm_one_reg *reg,
				  unsigned long rtype);
int kvm_riscv_vcpu_set_reg_vector(struct kvm_vcpu *vcpu,
				  const struct kvm_one_reg *reg,
				  unsigned long rtype);
#endif
