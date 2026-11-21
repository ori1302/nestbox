#!/bin/sh
# Shows how much of your Claude plan's 5-hour and weekly limits is used, as two gauges on the pet, while Claude works.
# Run by the Claude Code hooks in hooks.json; copy it to ~/.nestbox/claude-usage.sh.
#
# The numbers come from Claude Code itself: `claude -p /usage` prints them, with Claude Code's own sign-in, which it
# renews as it needs to. Nothing here reads a sign-in. It asks at most every 5 minutes, and after a failure waits
# 10, 20, then 30 minutes. Any other source can fill the same file: the statusLine in hooks.json does when Claude
# Code runs in a terminal.

N=~/.nestbox
USAGE=$N/claude-usage.json      # {"five_hour": {"used_percentage", "resets_at"}, "seven_day": {...}}
NEXT=$N/claude-usage.next       # the earliest time to ask again, and how many misses in a row
LAST=$N/claude-usage.last       # what /usage printed last, for when the gauges don't show
DEVICE=$N/device
ASK_TIMEOUT_S=30

# "Current session: 81% used · resets Nov 11 at 2:59pm (Europe/London)" -> {"used_percentage": 81, "resets_at": …}
window() {
    line=$(printf '%s\n' "$out" | grep -m1 "^$1")
    pct=$(printf '%s' "$line" | sed -n 's/^[^:]*: *\([0-9][0-9.]*\)% used.*/\1/p')
    [ -n "$pct" ] || { echo null; return; }
    when=$(printf '%s' "$line" |
        sed -n 's/.* resets \([A-Z][a-z]* [0-9][0-9]*\) at \([0-9:]*[ap]m\) (\([^)]*\)).*/\1|\2|\3/p')
    at=null
    if [ -n "$when" ]; then
        day=${when%%|*}; rest=${when#*|}; clock=${rest%%|*}; zone=${rest#*|}
        case $clock in *:*) ;; *) clock="${clock%??}:00${clock#"${clock%??}"}" ;; esac
        year=$(date +%Y)
        at=$(TZ=$zone date -j -f "%b %d %Y %I:%M%p %S" "$day $year $clock 00" +%s 2>/dev/null) || at=null
        [ "$at" != null ] && [ "$at" -lt $((now - 86400)) ] &&
            at=$(TZ=$zone date -j -f "%b %d %Y %I:%M%p %S" "$day $((year + 1)) $clock 00" +%s 2>/dev/null)
    fi
    echo "{\"used_percentage\": $pct, \"resets_at\": ${at:-null}}"
}

# The Claude config folder of the account Claude runs as: CLAUDE_CONFIG_DIR, or the folder whose account matches
# CLAUDE_CODE_ACCOUNT_UUID (the Claude desktop app sets only that); with neither, Claude Code's default.
config_dir() {
    [ -n "${CLAUDE_CONFIG_DIR:-}" ] && { echo "$CLAUDE_CONFIG_DIR"; return; }
    [ -n "${CLAUDE_CODE_ACCOUNT_UUID:-}" ] || return
    for d in "$HOME"/.claude-* "$HOME/.claude"; do
        f="$d/.claude.json"
        [ "$d" = "$HOME/.claude" ] && [ ! -f "$f" ] && f="$HOME/.claude.json"
        [ -d "$d" ] && [ "$(jq -r '.oauthAccount.accountUuid // empty' "$f" 2>/dev/null)" = \
            "$CLAUDE_CODE_ACCOUNT_UUID" ] && { echo "$d"; return; }
    done
}

now=$(date +%s)
read -r next misses 2>/dev/null <"$NEXT" || { next=0; misses=0; }
if [ "$now" -ge "${next:-0}" ]; then
    echo "$((now + 300)) ${misses:-0}" >"$NEXT"     # also keeps other hooks running now from asking at once
    claude=$(command -v claude || echo "$HOME/.local/bin/claude")
    dir=$(config_dir)
    # A clean environment, so this is a Claude Code of its own and not part of the session that ran the hook; no
    # hooks (they would run this again), no MCP servers, nothing saved.
    out=$(cd "${TMPDIR:-/tmp}" && env -i HOME="$HOME" USER="${USER:-$(id -un)}" LOGNAME="${LOGNAME:-$(id -un)}" \
        PATH="$PATH" SHELL="${SHELL:-/bin/zsh}" TMPDIR="${TMPDIR:-/tmp}" LANG="${LANG:-en_US.UTF-8}" \
        ${dir:+"CLAUDE_CONFIG_DIR=$dir"} "$claude" -p /usage --settings '{"disableAllHooks":true}' \
        --strict-mcp-config --no-session-persistence --max-turns 1 2>&1 &
        pid=$!; (sleep $ASK_TIMEOUT_S; kill $pid 2>/dev/null) >/dev/null 2>&1 & wait $pid)
    printf 'config folder: %s\n%s\n' "${dir:-default}" "$out" >"$LAST"
    five=$(window "Current session")
    week=$(window "Current week (all models)")
    [ "$week" = null ] && week=$(window "Current week")
    if [ "$five" != null ] || [ "$week" != null ]; then
        printf '{"five_hour": %s, "seven_day": %s}\n' "$five" "$week" | jq -c . >"$USAGE.$$" &&
            mv -f "$USAGE.$$" "$USAGE"
        echo "$((now + 300)) 0" >"$NEXT"
    else
        misses=$((${misses:-0} + 1))
        echo "$((now + (misses >= 3 ? 1800 : misses * 600))) $misses" >"$NEXT"
    fi
fi

# The gauges: a window whose reset has passed is skipped; one that hasn't started yet (no reset time) shows its
# number all the same.
[ -s "$USAGE" ] && [ -s "$DEVICE" ] || exit 0
jq -c '(.five_hour | {k: "5h", v: .}), (.seven_day | {k: "wk", v: .})
       | select(.v != null and (.v.resets_at == null or .v.resets_at > now))
       | (.v.used_percentage | round) as $p
       | {from: "claude", id: ("usage/" + .k), state: "on", line: false, gauge: $p, label: .k,
          color: (if $p >= 90 then "red" else "orange" end), for: "2m"}' "$USAGE" |
    while read -r tell; do
        curl -s -m 2 "$(head -1 "$DEVICE")/v1/tell" -H "Authorization: Bearer $(sed -n 's/^key=//p' "$DEVICE")" \
            -d "$tell" >/dev/null
    done
exit 0
