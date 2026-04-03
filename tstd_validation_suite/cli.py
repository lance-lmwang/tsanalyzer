import argparse
import sys
import os
import glob
from core.runner import TSTDRunner, load_scenario
# from core.report import generate_summary_report # Placeholder

def run_scenario(scenario_path):
    print(f"🚀 [RUN] {scenario_path}")
    scenario_cfg = load_scenario(scenario_path)
    runner = TSTDRunner(scenario_cfg)
    df = runner.run()
    # evaluate_assertions(scenario_cfg['assertions'], df)
    return df

def main():
    parser = argparse.ArgumentParser(description="T-STD Validation Suite CLI")
    subparsers = parser.add_subparsers(dest="command")

    # Command: run
    run_parser = subparsers.add_parser("run", help="Run a single scenario")
    run_parser.add_argument("path", help="Path to scenario YAML")

    # Command: run-all
    subparsers.add_parser("run-all", help="Run all scenarios in the matrix")

    # Command: report
    report_parser = subparsers.add_parser("report", help="Generate validation report")
    report_parser.add_argument("--trace-dir", default="outputs/traces")

    # Command: ci
    subparsers.add_parser("ci", help="CI/CD gate mode")

    args = parser.parse_args()

    if args.command == "run":
        run_scenario(args.path)
    elif args.command == "run-all":
        scenarios = glob.glob("scenarios/*.yaml")
        for s in scenarios:
            run_scenario(s)
    elif args.command == "report":
        print("📊 [REPORT] Generating HTML report...")
        # generate_summary_report()
    elif args.command == "ci":
        print("🧪 [CI] Executing full validation suite gate...")
        # CI Logic: exit 1 if any scenario fails
    else:
        parser.print_help()

if __name__ == "__main__":
    main()
