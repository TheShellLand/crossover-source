import re
import json
import requests
import pandas as pd


def search(func):
    def inner(version) -> re:
        regex_ver = r'([0-9]*[.][0-9]*[.][0-9]*)'
        r = re.search(regex_ver, f'{version}')
        if r:
            return r.groups()[0]

        return func(version)

    return inner


@search
def get_ver(version) -> re or None:
    return


def download_source(url) -> requests:
    return requests.get(url)


def main():
    source_url = 'https://media.codeweavers.com/pub/crossover/source'
    source_name = 'crossover-sources'

    html = pd.read_html(source_url)

    html = html[0]
    html = html.drop(columns=html.columns[0])
    html = html.drop([*range(0, 2, 1)])

    filtered = html.sort_values('Last modified', ascending=False)

    # https://media.codeweavers.com/pub/crossover/source/crossover-sources-21.1.0.tar.gz
    filtered['Url'] = filtered.Name.dropna().apply(lambda x: f'{source_url}/{x}')

    filtered['Version'] = filtered.Name.dropna().apply(lambda x: x if source_name in x else None).dropna()
    filtered.Version = filtered.Version.apply(get_ver)

    sources = filtered[filtered.Version.notna()]

    latest = sources.iloc[0]
    
    name = latest.Name
    url = latest.Url
    version = latest.Version

    latest_dict = dict(
        name=latest.Name,
        url=latest.Url,
        version=latest.Version
    )

    with open(name, 'wb') as f:
        f.write(download_source(url).content)

    with open(f'{name}.json', 'w') as f:
        f.write(json.dumps(latest_dict))

    print(latest_dict)


if __name__ == "__main__":
    main()
