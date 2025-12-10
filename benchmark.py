import subprocess
import json
import os


def extract_json_from_clog(clog_text):
    for line in clog_text.splitlines():
        if line.startswith("[nanobench]"):
            # remove prefix
            json_part = line[len("[nanobench] ") :]
            return json.loads(json_part)
    raise ValueError("No nanobench JSON output found!")


def run_benchmark(scene, epoch):
    task = subprocess.run(
        ["./benchmark.sh", str(scene), str(epoch)],
        cwd=os.path.abspath("./tracer/"),
        capture_output=True,
    )

    data = extract_json_from_clog(task.stderr)
    return stderr

    # return {
    #     "scene": scene,
    #     "epoch": epoch,
    #     "bench": data,
    # }


def main():
    print(run_benchmark(2, 1))

    # scenes_to_test = range(0, 3)

    # for i in scenes_to_test:
    # output = subprocess.run([])
    # print(f"Running {i}")


if __name__ == "__main__":
    main()
