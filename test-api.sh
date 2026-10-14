#!/usr/bin/env bash
set -euo pipefail

# Usage: SMARTCLOCK_PASSWORD=... ./test-api.sh <hostname-or-IP>
# Previews brightness and discards that preview; refuses existing display drafts.
if [[ $# != 1 ]]; then
    echo "Usage: SMARTCLOCK_PASSWORD=... $0 <hostname-or-IP>" >&2
    exit 2
fi
command -v curl >/dev/null
command -v jq >/dev/null
base_url="http://$1"
if [[ -z ${SMARTCLOCK_PASSWORD:-} ]]; then
    read -rsp 'Admin password: ' SMARTCLOCK_PASSWORD
    echo
fi
umask 077
test_dir=$(mktemp -d)
preview_started=false
cleanup() {
    if $preview_started; then
        curl -fsS -b "$test_dir/cookies" -X POST "$base_url/dashboard/discard" >/dev/null || true
    fi
    rm -rf "$test_dir"
}
trap cleanup EXIT
request() {
    local expected=$1 path=$2
    shift 2
    local status
    status=$(curl -sS --connect-timeout 5 --max-time 30 \
        -b "$test_dir/cookies" -c "$test_dir/cookies" \
        -o "$test_dir/response" -w '%{http_code}' "$base_url$path" "$@")
    if [[ $status != "$expected" ]]; then
        echo "$path: expected HTTP $expected, received $status" >&2
        exit 1
    fi
}
jq -n --arg password "$SMARTCLOCK_PASSWORD" '{username:"admin", password:$password}' > "$test_dir/login"
request 200 /auth/login -H 'Content-Type: application/json' --data-binary "@$test_dir/login"
rm "$test_dir/login"
unset SMARTCLOCK_PASSWORD
request 200 /app.json
jq -e '.displayDraft == false and (.brt | type == "number")' "$test_dir/response" >/dev/null
original_brightness=$(jq -r '.brt' "$test_dir/response")
for path in /dashboard.json /feeds.json /space.json /version.json /brt.json; do
    request 200 "$path"
    jq -e 'type == "object"' "$test_dir/response" >/dev/null
    echo "$path OK"
done
request 400 /api/update -H 'Content-Type: application/json' --data-binary '{"line1":123}'
request 200 /app.json
preview_brightness=50
if [[ $original_brightness == 50 ]]; then preview_brightness=51; fi
preview_started=true
request 200 /dashboard/live -H 'Content-Type: application/json' \
    --data-binary "{\"settings\":{\"brightness\":$preview_brightness}}"
request 200 /app.json
jq -e --argjson value "$preview_brightness" '.brt == $value and .displayDraft' "$test_dir/response" >/dev/null
request 200 /dashboard/discard -X POST
preview_started=false
request 200 /app.json
jq -e --argjson value "$original_brightness" '.brt == $value and .displayDraft == false' "$test_dir/response" >/dev/null
request 200 /auth/logout -X POST
request 401 /app.json
echo 'Authenticated API checks passed; brightness restored.'
