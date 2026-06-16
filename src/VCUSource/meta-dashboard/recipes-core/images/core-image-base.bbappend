# Append the VCU dashboard binary to whatever base image is selected.
# Works with core-image-base, core-image-minimal, or any custom image
# that includes this layer in BBLAYERS.
IMAGE_INSTALL:append = " vcu-dashboard autologin"
