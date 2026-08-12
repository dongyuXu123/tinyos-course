# Lesson B23: 端到端综合验收 —— 终课（无启动镜像）
# B23 的"产物"是回归能力与验收文档：
#   build/course-map.txt  课程地图（make 生成，提交产物）
#   build/acceptance.txt  验收报告（make acceptance 跑全课程回归后生成）
# make check 只做静态断言，绝不递归调用 validate-course.sh
# （validate-course.sh all check 会进入本课的 check，递归会死循环）。
BUILD := build

.PHONY: all check acceptance clean

all: $(BUILD)/course-map.txt

$(BUILD):
	mkdir -p $@

$(BUILD)/course-map.txt: course.py | $(BUILD)
	python3 course.py map > $@

check: $(BUILD)/course-map.txt
	python3 course.py check
	@test "$$(wc -l < $(BUILD)/course-map.txt)" -eq 24 \
	  || { printf 'FAIL: course-map.txt row count\n' >&2; exit 1; }
	@printf 'B23 check PASS: course map + acceptance artifacts\n'

# 终课验收动作：全课程回归（在临时副本执行，不改写 stable 产物）。
# 较慢（约 10 分钟），按需手动执行；结果写入 build/acceptance.txt。
acceptance: FORCE
	../../scripts/validate-course.sh all check
	../../scripts/validate-course.sh all qemu
	@mkdir -p $(BUILD)
	@printf 'Mini-GRUB B23 acceptance run: %s\n' "$$(date -Is)" > $(BUILD)/acceptance.txt
	@printf 'all check: PASS\nall qemu: PASS\n' >> $(BUILD)/acceptance.txt
	@printf 'B23 acceptance PASS: full course regression green\n'

FORCE:

clean:
	rm -rf $(BUILD)
