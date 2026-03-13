import matplotlib.pyplot as plt
import numpy as np
from scipy import stats

# Data from Part II and Part III
loads = [5, 25, 50, 75, 100, 125, 150, 175, 200]
means = [200, 200, 99.5, 64, 45, 41, 36, 34, 28]  # messages delivered

# Standard deviations (only have for 50% from our 4 seeds)
stds = [0, 0, 4.12, 0, 0, 0, 0, 0, 0]

n = 4
t_crit = 2.353  # t-critical for 90% CI with df=3

# Calculate confidence intervals (only for 50% load point)
cis = []
for i in range(len(loads)):
    if loads[i] == 50:
        ci = t_crit * (stds[i] / np.sqrt(n))
        cis.append(ci)
    else:
        cis.append(0)

# Create plot
plt.figure(figsize=(12, 7))
plt.errorbar(loads, means, yerr=cis, fmt='o-', capsize=8, capthick=2,
             ecolor='red', color='blue', linewidth=2.5, markersize=10,
             markeredgecolor='darkblue', markeredgewidth=1.5)

# Customize plot
plt.xlabel('Offered Load (% of link capacity)', fontsize=13, fontweight='bold')
plt.ylabel('Messages Delivered (out of 200)', fontsize=13, fontweight='bold')
plt.title('Congestion Collapse: Goodput vs Load with 90% Confidence Interval', fontsize=14, fontweight='bold')
plt.grid(True, alpha=0.3, linestyle='--')
plt.xticks(loads, fontsize=10)
plt.yticks(fontsize=10)
plt.ylim(0, 220)

# Add horizontal line at 100 messages (50% delivery)
plt.axhline(y=100, color='gray', linestyle='--', alpha=0.7, label='50% delivery line')
plt.axhline(y=60, color='orange', linestyle='--', alpha=0.7, label='30% delivery line')

# Add annotation for CI
plt.annotate(f'90% Confidence Interval at 50% load:\n[{means[2]-cis[2]:.1f}, {means[2]+cis[2]:.1f}] messages',
             xy=(50, 120), xytext=(80, 150),
             arrowprops=dict(arrowstyle='->', color='red', linewidth=2),
             fontsize=11, bbox=dict(boxstyle='round,pad=0.5', facecolor='lightyellow', edgecolor='red', alpha=0.9))

# Add collapse annotation
plt.annotate('COLLAPSE BEGINS', xy=(50, 99), xytext=(30, 170),
             arrowprops=dict(arrowstyle='->', color='orange', linewidth=2),
             fontsize=11, fontweight='bold', color='darkorange')

# Add legend
plt.legend(loc='upper right', fontsize=10)

# Add text box with statistics
stats_text = f'Statistics at 50% Load (n=4 seeds):\nMean = {means[2]:.1f} msgs\nStd Dev = {stds[2]:.2f}\n90% CI = [{means[2]-cis[2]:.1f}, {means[2]+cis[2]:.1f}]'
plt.text(120, 50, stats_text, fontsize=10,
         bbox=dict(boxstyle='round,pad=0.5', facecolor='lightblue', alpha=0.8))

plt.tight_layout()
plt.savefig('goodput_with_ci.png', dpi=300, bbox_inches='tight')
print("Plot saved as 'goodput_with_ci.png'")

# Also create a data table for the report
print("\n" + "="*60)
print("PART III - STATISTICAL RIGOR RESULTS TABLE")
print("="*60)
print(f"{'Load':>6} {'Mean':>10} {'StdDev':>10} {'CI_Low':>10} {'CI_High':>10}")
print("-"*60)
for i in range(len(loads)):
    ci_low = means[i] - cis[i]
    ci_high = means[i] + cis[i]
    print(f"{loads[i]:>6} {means[i]:>10.1f} {stds[i]:>10.2f} {ci_low:>10.1f} {ci_high:>10.1f}")
print("="*60)
