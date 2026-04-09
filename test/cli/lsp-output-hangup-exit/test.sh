#!/usr/bin/env bash
set -euo pipefail

ruby <<'RUBY'
require 'timeout'

stdin_r, stdin_w = IO.pipe
stdout_r, stdout_w = IO.pipe

pid = Process.spawn(
  'main/sorbet',
  '--silence-dev-message',
  '--lsp',
  '--disable-watchman',
  '--dir',
  'test/cli/lsp-output-hangup-exit',
  in: stdin_r,
  out: stdout_w,
  err: File::NULL,
)

stdin_r.close
stdout_w.close

sleep 2

before =
  begin
    Process.kill(0, pid)
    0
  rescue Errno::ESRCH
    1
  end

# Keep stdin open so the process only exits because stdout hangs up.
stdout_r.close

status = Timeout.timeout(5) do
  _, child_status = Process.wait2(pid)
  child_status
end

after =
  begin
    Process.kill(0, pid)
    0
  rescue Errno::ESRCH
    1
  end

exit_code = if status.exited?
  status.exitstatus
else
  128 + status.termsig
end

puts "before=#{before}"
puts "exit=#{exit_code}"
puts "after=#{after}"

stdin_w.close
RUBY
