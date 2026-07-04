# Copies SRC directory's contents into DST, but only if SRC exists.
# Run as a POST_BUILD step (cmake -P) so assets added after the project was configured still get
# copied on the next build — no reconfigure required.
if(EXISTS "${SRC}")
    file(MAKE_DIRECTORY "${DST}")
    file(COPY "${SRC}/" DESTINATION "${DST}")
endif()
