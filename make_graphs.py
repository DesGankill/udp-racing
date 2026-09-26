import pandas as pd
import numpy as np
import matplotlib.pyplot as plt
from pathlib import Path


INPUT_FILE = "results.csv"
OUTPUT_DIR = Path("graphs")

OUTPUT_DIR.mkdir(exist_ok=True)

df = pd.read_csv(INPUT_FILE, sep=";")

experiments = [
    "baseline",
    "delay_50",
    "delay_100",
    "jitter",
    "loss_5",
    "combined",
]

received = df[df["status"] == "received"]

stats = []

for experiment in experiments:
    group = df[df["experiment_id"] == experiment]
    rtt = group.loc[group["status"] == "received", "rtt_ms"].to_numpy()

    if len(rtt) > 1:
        jitter = np.abs(np.diff(rtt)).mean()
    else:
        jitter = 0.0

    stats.append({
        "experiment": experiment,
        "sent": len(group),
        "received": len(rtt),
        "timeout": (group["status"] == "timeout").sum(),
        "min_rtt": rtt.min(),
        "max_rtt": rtt.max(),
        "mean_rtt": rtt.mean(),
        "median_rtt": np.median(rtt),
        "jitter": jitter,
        "loss": (group["status"] == "timeout").sum()
        / len(group) * 100,
    })

stats = pd.DataFrame(stats)


# 1. Средний RTT
plt.figure(figsize=(10, 6))

plt.bar(
    stats["experiment"],
    stats["mean_rtt"]
)

plt.xlabel("Эксперимент")
plt.ylabel("Средний RTT, мс")
plt.title("Средний RTT по экспериментам")
plt.xticks(rotation=20)
plt.tight_layout()

plt.savefig(
    OUTPUT_DIR / "01_mean_rtt.png",
    dpi=200
)

plt.close()


# 2. Диапазон RTT (P5-P95)
plt.figure(figsize=(10, 6))

x = np.arange(len(stats))

p5 = []
p95 = []
median = []

for experiment in experiments:
    rtt = received.loc[
        received["experiment_id"] == experiment,
        "rtt_ms"
    ].to_numpy()

    p5.append(np.percentile(rtt, 5))
    p95.append(np.percentile(rtt, 95))
    median.append(np.median(rtt))

p5 = np.array(p5)
p95 = np.array(p95)
median = np.array(median)

plt.errorbar(
    x,
    median,
    yerr=[
        median - p5,
        p95 - median,
    ],
    fmt="o",
    capsize=5
)

plt.xticks(
    x,
    experiments,
    rotation=20
)

plt.xlabel("Эксперимент")
plt.ylabel("RTT, мс")
plt.title("Медиана и диапазон P5-P95 RTT")
plt.tight_layout()

plt.savefig(
    OUTPUT_DIR / "02_rtt_range.png",
    dpi=200
)

plt.close()


# 3. Jitter и потери
fig, ax1 = plt.subplots(figsize=(10, 6))

x = np.arange(len(stats))

bars = ax1.bar(
    x - 0.2,
    stats["jitter"],
    width=0.4,
    label="Jitter"
)

ax1.set_xlabel("Эксперимент")
ax1.set_ylabel("Jitter, мс")

ax1.set_xticks(x)
ax1.set_xticklabels(
    stats["experiment"],
    rotation=20
)

ax2 = ax1.twinx()

ax2.bar(
    x + 0.2,
    stats["loss"],
    width=0.4,
    label="Loss"
)

ax2.set_ylabel("Потери, %")

plt.title("Jitter и потери пакетов")

fig.tight_layout()

plt.savefig(
    OUTPUT_DIR / "03_jitter_loss.png",
    dpi=200
)

plt.close()

print("Графики успешно созданы:")
print(OUTPUT_DIR / "01_mean_rtt.png")
print(OUTPUT_DIR / "02_rtt_range.png")
print(OUTPUT_DIR / "03_jitter_loss.png")