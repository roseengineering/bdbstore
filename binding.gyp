{
    'targets': [
        {
            'target_name': 'bdbstore',
            'link_settings': {
                'libraries': [ '-ldb' ],
            },
            'sources': [ 'bdbstore.cc' ]
        }
    ]
}
