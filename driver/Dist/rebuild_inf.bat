build_inf.exe uata2k.in_ > uata2k.inf
copy uata2k.in_ uata_nt4.inf
srchrep -src ";DEVLIST" -dest "%%DeviceDesc%%" uata_nt4.inf
srchrep -src ";DEVNAMELIST" -dest "DeviceDesc" uata_nt4.inf
copy uata2k.inf uata2kh.inf
srchrep -src "Class=SCSIAdapter" -dest "Class=hdc" uata2kh.inf
srchrep -src "ClassGuid={4D36E97B-E325-11CE-BFC1-08002BE10318}" -dest "ClassGuid={4D36E96A-E325-11CE-BFC1-08002BE10318}" uata2kh.inf
