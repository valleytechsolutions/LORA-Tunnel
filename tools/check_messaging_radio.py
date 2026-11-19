"""Exercise actual messaging radio transitions with a bounded synthetic SX1262.

Compiles scanner function bodies from production source for Heltec/Cardputer.
Driver IRQs, SPI contention and failures are simulated; no device is accessed.
"""
from pathlib import Path
import os
import subprocess
import tempfile
from check_cardputer_ui import ROOT, compiler
from check_lora_hopping import extract


def check():
    source = (ROOT / "src/scanners.cpp").read_text(encoding="utf-8")
    actual = "#include \"board_config.h\"\n#if defined(LORA_TUNNEL_HAS_LORA_FEM)\n"
    actual += extract(source, "Module::RfSwitchMode_t frontendModes[]=") + ";\n#endif\n"
    actual += extract(source, "struct RadioBusLock {") + ";\n"
    for signature in ["void accountRadioListen()", "void stopRadioListen()", "int armRadioReceive()",
                      "bool applyRadioProfile(", "void installTransmitSwitching()",
                      "void serviceRadioTransition()", "bool restoreChatReceiveLocked()",
                      "void serviceMessages()", "bool scannersConfigureLoRa()",
                      "bool scannersMessagesBegin(", "bool scannersMessagesCancelTx()", "bool scannersMessagesEnd()"]:
        actual += extract(source, signature) + "\n"
    fixture = (ROOT / "tests/scanner_message_host/main.cpp").read_text(encoding="utf-8")
    if fixture.count("// SCANNER_IMPLEMENTATION") != 1:
        raise RuntimeError("Scanner fixture marker is invalid")
    with tempfile.TemporaryDirectory(prefix="messaging-radio-") as temporary:
        work = Path(temporary)
        unit = work / "radio.cpp"
        unit.write_text(fixture.replace("// SCANNER_IMPLEMENTATION", actual), encoding="utf-8")
        cxx = compiler()
        environment = os.environ.copy()
        environment["PATH"] = str(Path(cxx).parent) + os.pathsep + environment.get("PATH", "")
        results = []
        for target, defines in (("Heltec", []), ("Cardputer", ["-DLORA_TUNNEL_CARDPUTER_ADV"])):
            binary = work / (target + (".exe" if os.name == "nt" else ""))
            command = [cxx, "-std=c++14", "-O1", "-Wall", "-Wextra", *defines,
                       "-I", str(ROOT / "include"), str(unit), "-o", str(binary)]
            if os.name == "nt": command += ["-static-libgcc", "-static-libstdc++"]
            subprocess.run(command, cwd=ROOT, env=environment, check=True)
            result = subprocess.run([str(binary)], cwd=ROOT, env=environment, capture_output=True, text=True)
            if result.returncode:
                raise RuntimeError(target + ": " + result.stdout + result.stderr)
            results.append(target + ": " + result.stdout.strip())
        return "\n".join(results)


if __name__ == "__main__":
    print(check())
