// Sign latest.json for the configurator app: an Ed25519 signature over the
// file's exact bytes, base64, written to latest.json.sig. The app refuses a
// feed no trusted key signed (DS5Dongle-OLED-Config-Web
// electron/main/feedSignature.ts holds the public keys).
//   MANIFEST_SIGNING_KEY="$(cat key.pem)" node tools/sign_manifest.mjs latest.json
// The key is a GitHub Actions secret; without it this fails, so CI can never
// publish an unsigned feed.
import { createPrivateKey, createPublicKey, sign, verify } from "node:crypto";
import { readFileSync, writeFileSync } from "node:fs";

const file = process.argv[2] ?? "latest.json";
const pem = process.env.MANIFEST_SIGNING_KEY;
if (!pem) {
  console.error("MANIFEST_SIGNING_KEY is not set: refusing to publish an unsigned feed.");
  process.exit(1);
}
const key = createPrivateKey(pem);
const data = readFileSync(file);
const signature = sign(null, data, key);
if (!verify(null, data, createPublicKey(key), signature)) throw new Error("signature self-check failed");
writeFileSync(`${file}.sig`, signature.toString("base64") + "\n");
const pub = createPublicKey(key).export({ format: "der", type: "spki" }).toString("base64");
console.log(`${file}.sig written (key ${pub})`);
