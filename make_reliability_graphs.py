"""Build reliability graphs from docs/reliability_samples.csv."""

from pathlib import Path

import matplotlib.pyplot as plt
import pandas as pd


INPUT_FILE = Path("docs/reliability_samples.csv")
OUTPUT_DIR = Path("docs/graphs")

LOSS_SERIES = ["baseline", "loss_5", "loss_10", "loss_20"]
JITTER_SERIES = "jitter_loss_10"


def main() -> None:
    if not INPUT_FILE.exists():
        raise SystemExit(f"Missing measurements: {INPUT_FILE}")

    frame = pd.read_csv(INPUT_FILE, sep=";")
    OUTPUT_DIR.mkdir(parents=True, exist_ok=True)

    loss = frame[frame["series_id"].isin(LOSS_SERIES)].copy()
    summary = (
        loss.groupby("series_id", sort=False)
        .agg(
            loss_percent=("loss_percent", "first"),
            avg_attempts=("attempts", "mean"),
            sent=("sample", "count"),
        )
        .reset_index()
    )
    summary["series_id"] = pd.Categorical(
        summary["series_id"],
        LOSS_SERIES,
        ordered=True,
    )
    summary = summary.sort_values("series_id")

    plt.figure(figsize=(8, 5))
    plt.plot(
        summary["loss_percent"],
        summary["avg_attempts"],
        marker="o",
        label="Среднее число попыток",
    )
    plt.xlabel("Потери пакетов, %")
    plt.ylabel("Среднее число попыток на команду")
    plt.title("Зависимость среднего числа попыток от процента потерь")
    plt.grid(True, linestyle="--", alpha=0.5)
    plt.legend()
    plt.tight_layout()
    attempts_path = OUTPUT_DIR / "attempts_vs_loss.png"
    plt.savefig(attempts_path, dpi=200)
    plt.close()

    jitter = frame[frame["series_id"] == JITTER_SERIES]
    if jitter.empty:
        raise SystemExit(f"Series {JITTER_SERIES} is missing from {INPUT_FILE}")

    plt.figure(figsize=(8, 5))
    plt.plot(
        jitter["sample"],
        jitter["rto_ms"],
        marker=".",
        label="RTO",
    )
    plt.xlabel("Номер команды в серии")
    plt.ylabel("RTO, мс")
    plt.title("Изменение RTO во времени, серия jitter_loss_10")
    plt.grid(True, linestyle="--", alpha=0.5)
    plt.legend()
    plt.tight_layout()
    rto_path = OUTPUT_DIR / "rto_jitter_loss_10.png"
    plt.savefig(rto_path, dpi=200)
    plt.close()

    print(summary.to_string(index=False))
    print(attempts_path)
    print(rto_path)


if __name__ == "__main__":
    main()
