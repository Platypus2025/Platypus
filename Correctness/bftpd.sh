#!/usr/bin/env bash

set -euo pipefail

ROOT_DIR="$PWD/.."
BFTPD_BIN="${ROOT_DIR}/binaries/artifact_binaries_instrumented/bftpd"

if [[ ! -x "$BFTPD_BIN" ]]; then
  echo "Error: executable not found: $BFTPD_BIN"
  exit 1
fi

for cmd in ftp mktemp cmp grep sed id sleep pkill; do
  if ! command -v "$cmd" >/dev/null 2>&1; then
    echo "Error: required command not found: $cmd"
    exit 1
  fi
done

HOST="127.0.0.1"
PORT="2121"
FTP_USER="testuser"
FTP_PASS="testpass"
FTP_GROUP="$(id -gn)"

TMPDIR_ROOT="$(mktemp -d)"
TEST_AREA="${TMPDIR_ROOT}/ftp-area"
AUTH_FILE="${TMPDIR_ROOT}/ftpd.passwd"
CONF_FILE="${TMPDIR_ROOT}/bftpd.conf"
FTP_OUT="${TMPDIR_ROOT}/ftp_output.txt"

REMOTE_TESTDIR="${TEST_AREA}/testdir"
REMOTE_UPLOAD="${REMOTE_TESTDIR}/upload.txt"
REMOTE_RENAMED="${REMOTE_TESTDIR}/file2.txt"
REMOTE_SERVER_FILE="${TEST_AREA}/server.txt"

cleanup() {
  set +e
  pkill -f "${BFTPD_BIN} -d -c ${CONF_FILE}" >/dev/null 2>&1 || true
  rm -rf "$TMPDIR_ROOT"
}
trap cleanup EXIT

fail() {
  echo "[FAIL] $1"
  if [[ -f "$FTP_OUT" ]]; then
    echo "----- ftp output begin -----"
    sed -n '1,250p' "$FTP_OUT"
    echo "----- ftp output end -----"
  fi
  exit 1
}

pass() {
  echo "[OK] $1"
}

require_file_exists() {
  local path="$1"
  local context="$2"
  [[ -e "$path" ]] || fail "$context"
}

require_not_exists() {
  local path="$1"
  local context="$2"
  [[ ! -e "$path" ]] || fail "$context"
}

require_output_contains() {
  local needle="$1"
  local context="$2"
  grep -Fq "$needle" "$FTP_OUT" || fail "$context"
}

run_ftp() {
  local commands="$1"
  if ! ftp -inv >"$FTP_OUT" 2>&1 <<EOF
open $HOST $PORT
user $FTP_USER $FTP_PASS
$commands
bye
EOF
  then
    fail "ftp command sequence failed"
  fi
}

mkdir -p "$TEST_AREA"
printf 'hello-from-server\n' > "${TEST_AREA}/server.txt"
mkdir -p "${TEST_AREA}/preexisting_dir"
printf 'hello-from-client\n' > "${TMPDIR_ROOT}/upload.txt"

chmod -R 777 "$TMPDIR_ROOT"

printf '%s %s %s %s\n' \
  "$FTP_USER" "$FTP_PASS" "$FTP_GROUP" "$TMPDIR_ROOT" > "$AUTH_FILE"

cat > "$CONF_FILE" <<EOF
global{
  DENY_LOGIN="no"
  PORT="${PORT}"
  PASSIVE_PORTS="0"
  DATAPORT20="no"
  PATH_BFTPDUTMP=""
  LOGFILE=""
  AUTH="PASSWD"
  FILE_AUTH="${AUTH_FILE}"
  DO_CHROOT="no"
  LOG_WTMP="no"
  BIND_TO_ADDR="${HOST}"
  PATH_FTPUSERS="/nonexistent"
  AUTH_ETCSHELLS="no"
  ALLOWCOMMAND_DELE="yes"
  ALLOWCOMMAND_STOR="yes"
  ALLOWCOMMAND_SITE="no"
}
EOF

echo "[*] Temporary test area: $TEST_AREA"
echo "[*] Starting bftpd from: $BFTPD_BIN"
"$BFTPD_BIN" -d -c "$CONF_FILE"

sleep 1

run_ftp ""
require_output_contains "230 User logged in." "Login failed"
pass "bftpd started"

echo "[*] Verifying initial listing"
run_ftp "cd ${TEST_AREA}
pwd
ls"
require_output_contains "server.txt" "Initial listing did not contain server.txt"
require_output_contains "preexisting_dir" "Initial listing did not contain preexisting_dir"
pass "Initial LIST verified"

echo "[*] Creating directory testdir"
run_ftp "cd ${TEST_AREA}
mkdir testdir
ls"
require_file_exists "${REMOTE_TESTDIR}" "Directory testdir was not created"
pass "MKD verified"

echo "[*] Uploading file"
run_ftp "cd ${REMOTE_TESTDIR}
put ${TMPDIR_ROOT}/upload.txt upload.txt
ls"
require_file_exists "${REMOTE_UPLOAD}" "Uploaded file not found on server"
cmp -s "${TMPDIR_ROOT}/upload.txt" "${REMOTE_UPLOAD}" \
  || fail "Uploaded file contents do not match"
pass "STOR/upload verified"

echo "[*] Downloading file"
run_ftp "get ${REMOTE_SERVER_FILE} ${TMPDIR_ROOT}/download.txt"
require_file_exists "${TMPDIR_ROOT}/download.txt" "Downloaded file was not created locally"
cmp -s "${REMOTE_SERVER_FILE}" "${TMPDIR_ROOT}/download.txt" \
  || fail "Downloaded file contents do not match"
pass "RETR/download verified"

echo "[*] Renaming uploaded file"
run_ftp "rename ${REMOTE_UPLOAD} ${REMOTE_RENAMED}
cd ${REMOTE_TESTDIR}
ls"
require_file_exists "${REMOTE_RENAMED}" "Renamed file not found on server"
require_not_exists "${REMOTE_UPLOAD}" "Old file name still exists after rename"
pass "RNFR/RNTO verified"

echo "[*] Deleting renamed file"
run_ftp "delete ${REMOTE_RENAMED}
cd ${REMOTE_TESTDIR}
ls"
require_not_exists "${REMOTE_RENAMED}" "File still exists after delete"
pass "DELE verified"

echo "[*] Removing testdir"
run_ftp "cd ${TEST_AREA}
rmdir testdir
ls"
require_not_exists "${REMOTE_TESTDIR}" "Directory still exists after rmdir"
pass "RMD verified"

echo
echo "[SUCCESS] bftpd functionality checks completed successfully."