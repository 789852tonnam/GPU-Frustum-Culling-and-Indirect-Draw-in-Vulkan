from clock_guard import try_clock


class Mode:
    value = 'auto'
    def read_text(self): return self.value
    def write_text(self, value): self.value = value.strip()


class Overdrive:
    def __init__(self, fail=False):
        self.values = [600, 3000]
        self.fail = fail
    def read_text(self):
        return f'OD_SCLK:\n0: {self.values[0]}Mhz\n1: {self.values[1]}Mhz\nOD_RANGE:\n'
    def write_text(self, value):
        if self.fail and value.strip() == 's 1 2000':
            self.fail = False
            raise OSError('Injected clock-setting failure')
        if value.startswith('s '):
            _, index, mhz = value.split()
            self.values[int(index)] = int(mhz)


for fail in (False, True):
    mode, od = Mode(), Overdrive(fail)
    observed = []
    try:
        try_clock(mode, od, lambda limits: observed.append(limits))
    except OSError:
        assert fail
    assert mode.value == 'auto' and od.values == [600, 3000]
    assert observed == ([] if fail else [(2000, 2000)])
print('Clock guard success and failed-write rollback checks passed.')
