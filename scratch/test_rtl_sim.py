import sys

def simulate():
    # Angles
    STAGE_ANGLES = [3217, 1934, 1016, 515, 258, 129, 64, 32]

    def wrap16(v):
        v &= 0xFFFF
        return v - 65536 if v >= 32768 else v

    def fp_add_sub(a, b, op, sat):
        full = (a - b) if op else (a + b)
        overflow = not (-32768 <= full <= 32767)
        if sat and overflow:
            res = 32767 if full > 32767 else -32768
        else:
            res = wrap16(full)
        return res, overflow

    def asr(v, s):
        if s <= 0:
            return v
        bias = 1 << (s - 1)
        # Arithmetic right shift matching SV: (v + bias) >>> s
        return (v + bias) >> s

    def k_prescale(v):
        acc = (v << 11) + (v << 9) - (v << 6) - (v << 3)
        acc = acc + 2048
        return wrap16(acc >> 12)

    with open('rtl/tb/build/regression.mem', 'r') as f:
        lines = f.read().splitlines()

    count = int(lines[0])
    mismatches = 0

    for idx in range(1, count + 1):
        parts = list(map(int, lines[idx].split()))
        xi, yi, zi, sat, exp_x, exp_y, exp_z, exp_ovf = parts

        # Prescale
        x = k_prescale(xi)
        y = k_prescale(yi)
        z = wrap16(zi)

        overflow = False

        # 8 stages
        for stage_idx in range(8):
            sigma = 1 if z >= 0 else 0
            
            # asr
            x_sh = asr(x, stage_idx)
            y_sh = asr(y, stage_idx)

            x_mux = x_sh if sigma else -x_sh
            y_mux = y_sh if sigma else -y_sh

            x_new, ovf_x = fp_add_sub(x, y_mux, 1, sat)
            y_new, ovf_y = fp_add_sub(y, x_mux, 0, sat)

            angle = STAGE_ANGLES[stage_idx]
            z_b = angle if sigma else -angle
            z_new, _ = fp_add_sub(z, z_b, 1, 0) # z wraps

            overflow = ovf_x or ovf_y
            x, y, z = x_new, y_new, z_new

        if x != exp_x or y != exp_y or z != exp_z or overflow != exp_ovf:
            mismatches += 1
            if mismatches <= 5:
                print(f"Mismatch vector {idx-1}: got x={x} y={y} z={z} ovf={overflow}, exp x={exp_x} y={exp_y} z={exp_z} ovf={exp_ovf}")

    print(f"Total mismatches: {mismatches} / {count}")

if __name__ == '__main__':
    simulate()
