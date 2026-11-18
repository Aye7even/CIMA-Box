#!/usr/bin/env bash
# Makes the key pair that signs CIMA Box updates. Run once, in Git Bash, from the project folder:
#
#     bash tools/updates/make-update-key.sh
#
# It writes:
#   update_key.txt           the public key. Commit it: it's built into the program.
#   update_private_key.pem   the private key. NEVER commit or share it. Paste it into the GitHub
#                            repository secret UPDATE_SIGNING_KEY, then keep a backup somewhere safe
#                            (a password manager). Lose it and you need a new key, and everyone must
#                            install one new version by hand.
set -e
if [ -e update_private_key.pem ]; then
  echo "update_private_key.pem already exists. Not overwriting it."
  exit 1
fi
openssl ecparam -name prime256v1 -genkey -noout -out update_private_key.pem
openssl ec -in update_private_key.pem -pubout -outform DER 2>/dev/null | tail -c 64 | od -An -tx1 | tr -d ' \n' > update_key.txt
echo "Done."
echo "  update_key.txt          -> commit this"
echo "  update_private_key.pem  -> GitHub secret UPDATE_SIGNING_KEY (Settings > Secrets and variables > Actions)"
echo "                             then back it up and keep it out of git"
