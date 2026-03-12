#!/usr/bin/env python3
"""Restart a managed openpilot process, picking up any code changes.

Usage:
  python tools/process_restart.py webrtcd
  python tools/process_restart.py system.webrtc.webrtcd   # module path also works
"""
import argparse
import sys
import time

from openpilot.common.params import Params
from openpilot.system.manager.process_config import managed_processes


def resolve_process_name(name: str) -> str | None:
  if name in managed_processes:
    return name
  # try matching by module path
  for p_name, p in managed_processes.items():
    if hasattr(p, 'module') and p.module == name:
      return p_name
  return None


def main():
  parser = argparse.ArgumentParser(description="Restart a managed openpilot process")
  parser.add_argument("process", help="Process name (e.g. webrtcd) or module path (e.g. system.webrtc.webrtcd)")
  parser.add_argument("--timeout", type=float, default=10.0, help="Timeout in seconds waiting for manager to acknowledge")
  args = parser.parse_args()

  name = resolve_process_name(args.process)
  if name is None:
    print(f"Unknown process: {args.process}")
    print(f"Available: {', '.join(sorted(managed_processes.keys()))}")
    sys.exit(1)

  params = Params()
  params.put("ProcessRestart", name)
  print(f"Requested restart of '{name}'")

  start = time.monotonic()
  while time.monotonic() - start < args.timeout:
    if params.get("ProcessRestart") is None:
      print(f"'{name}' restart acknowledged by manager")
      return
    time.sleep(0.1)

  print("Warning: manager may not have processed the restart request (timeout)")
  sys.exit(2)


if __name__ == "__main__":
  main()
