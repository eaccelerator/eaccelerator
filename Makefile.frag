post-install:
	@echo ""
	@echo "+-------------------------------------------------------+"
	@echo "|                 !!! Attention !!!                     |"
	@echo "|                                                       |"
	@echo "| For disk cache users (using eaccelerator.shm_only=0): |"
	@echo "|                                                       |"
	@echo "| Please remember to empty your eAccelerator disk cache |"
	@echo "| when upgrading, otherwise things will break!          |"
	@echo "+-------------------------------------------------------+"
	@echo ""

install: $(all_targets) $(install_targets) post-install
