import sys
sys.path.append('reference/cordic_golden')
from cordic_golden import CordicGoldenModel

def test_corrected():
    def wrap16(v):
        v &= 0xFFFF
        return v - 65536 if v >= 32768 else v

    def fp_add_sub(a, b, op, sat_flag):
        full = (a - b) if op else (a + b)
        overflow = not (-32768 <= full <= 32767)
        if sat_flag and overflow:
            res = 32767 if full > 32767 else -32768
        else:
            res = wrap16(full)
        return res, overflow

    def asr_rtl(v, s):
        if s <= 0:
            return v
        bias = 1 << (s - 1)
        return (v + bias) >> s

    def k_prescale_rtl(v):
        acc = (v << 11) + (v << 9) - (v << 6) - (v << 3)
        acc = acc + 2048
        return wrap16(acc >> 12)

    STAGE_ANGLES_CORRECT = [3217, 1899, 1003, 509, 256, 128, 64, 32]

    with open('rtl/tb/build/regression.mem', 'r') as f:
        lines = f.read().splitlines()

    count = int(lines[0])
    mismatches = 0

    for idx in range(1, count + 1):
        parts = list(map(int, lines[idx].split()))
        xi, yi, zi, sat, exp_x, exp_y, exp_z, exp_ovf = parts

        x_r = k_prescale_rtl(xi)
        y_r = k_prescale_rtl(yi)
        z_r = wrap16(zi)

        overflow = False

        for stage_idx in range(8):
            sigma = 1 if z_r >= 0 else 0
            x_sh = asr_rtl(x_r, stage_idx)
            y_sh = asr_rtl(y_r, stage_idx)

            x_mux = x_sh if sigma else -x_sh
            y_mux = y_sh if sigma else -y_sh

            x_r, ovf_x = fp_add_sub(x_r, y_mux, 1, sat)
            y_r, ovf_y = fp_add_sub(y_r, x_mux, 0, sat)

            angle = STAGE_ANGLES_CORRECT[stage_idx]
            z_b = angle if sigma else -angle
            z_r, _ = fp_add_sub(z_r, z_b, 1, 0)

            overflow = ovf_x or ovf_y

        if x_r != exp_x or y_r != exp_y or z_r != exp_z or overflow != exp_ovf:
            mismatches += 1
            if mismatches <= 5:
                print(f"Mismatch vector {idx-1}: got x={x_r} y={y_r} z={z_r} ovf={overflow}, exp x={exp_x} y={exp_y} z={exp_z} ovf={exp_ovf}")

    print(f"Total mismatches with corrected angles: {mismatches} / {count}")

if __name__ == '__main__':
    test_corrected()
