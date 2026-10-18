"""Continuous resident-memory sampling for a set of root processes and every
descendant (px0's server, and Chrome's browser, renderer, GPU and utility
processes; floatinghotel's single process). RSS from ps, every 250 ms."""
import subprocess
import threading
import time


def tree(roots):
    seen, todo = set(), list(roots)
    while todo:
        pid = todo.pop()
        if pid in seen:
            continue
        seen.add(pid)
        children = subprocess.run(['pgrep', '-P', str(pid)], capture_output=True, text=True).stdout.split()
        todo.extend(int(c) for c in children)
    return seen


def snapshot(roots):
    pids = tree(roots)
    if not pids:
        return {}
    out = subprocess.run(['ps', '-o', 'pid=,rss=,comm=', '-p', ','.join(map(str, pids))], capture_output=True, text=True).stdout
    procs = {}
    for line in out.splitlines():
        parts = line.split(None, 2)
        if len(parts) == 3:
            procs[int(parts[0])] = {'rss_kb': int(parts[1]), 'name': parts[2].rsplit('/', 1)[-1]}
    return procs


class Sampler:
    def __init__(self, roots, interval=0.25):
        self.roots, self.interval, self.samples = list(roots), interval, []
        self._stop = threading.Event()
        self._thread = threading.Thread(target=self._run, daemon=True)

    def _run(self):
        while not self._stop.is_set():
            procs = snapshot(self.roots)
            self.samples.append({'t': time.time(), 'total_kb': sum(p['rss_kb'] for p in procs.values()),
                                 'processes': len(procs), 'by_name': _by_name(procs)})
            self._stop.wait(self.interval)

    def start(self):
        self._thread.start()
        return self

    def stop(self):
        self._stop.set()
        self._thread.join()
        return self.samples


def _by_name(procs):
    names = {}
    for p in procs.values():
        names[p['name']] = names.get(p['name'], 0) + p['rss_kb']
    return names
